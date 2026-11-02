/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2017 Nicolas Bonnefon
 * and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

// This file implements the LogMainView concrete class.
// Most of the actual drawing and event management is done in AbstractLogView
// Only behaviour specific to the main (top) view is implemented here.

#include "logmainview.h"

#include <QTextCodec>

#include "apisession.h"
#include "klogg/api_session.h"
#include "log.h"
#include "log_qt.h"
#include "overview.h"

#include "shortcuts.h"

namespace {

// Open/close races with the worker thread surface as these codes; they are
// expected until the file entry lands (or while it is being torn down), so
// the view falls back silently instead of logging once per painted line
bool isTransientApiError( klogg::api::ErrorCode code )
{
    return code == klogg::api::ErrorCode::FileNotOpen
           || code == klogg::api::ErrorCode::SearchCancelled;
}

} // namespace

LogMainView::LogMainView( const QuickFindPattern* const quickFindPattern, Overview* overview,
                          OverviewWidget* overview_widget, std::optional<klogg::api::FileId> fileId,
                          QWidget* parent )
    : AbstractLogView( quickFindPattern, parent )
    , fileId_( fileId )
{
    refreshFileInfoCache();
    setSearchLimits( 0_lnum, LineNumber( cachedNbLines_.load().get() ) );

    // The main data has a real (non NULL) Overview
    setOverview( overview, overview_widget );
}

void LogMainView::updateData()
{
    // The API-side FileInfo is refreshed on every engine loadingFinished, so
    // pulling it here keeps the caches in sync after growth and reloads
    refreshFileInfoCache();
    AbstractLogView::updateData();
}

void LogMainView::refreshFileInfoCache()
{
    if ( !fileId_ ) {
        return;
    }

    const auto info = klogg::ui::apiSession().fileInfo( *fileId_ );
    if ( info ) {
        cachedNbLines_ = info->total_lines;
        cachedMaxLineLength_ = info->max_line_length;
        return;
    }

    if ( !isTransientApiError( info.error().code ) ) {
        LOG_WARNING << "LogMainView: fileInfo failed, keeping the cached values: "
                    << info.error().message;
    }
}

std::string LogMainView::getLineString( LineNumber line ) const
{
    const auto lines = getLines( line, 1_lcount );
    return lines.empty() ? std::string{} : lines.front();
}

std::string LogMainView::getExpandedLineString( LineNumber line ) const
{
    return untabify( getLineString( line ) );
}

klogg::vector<std::string> LogMainView::getLines( LineNumber firstLine, LinesCount number ) const
{
    klogg::vector<std::string> lines;
    if ( fileId_ ) {
        // Mutable so the text can be moved out of the returned lines instead
        // of copied one more time on the paint path
        auto result = klogg::ui::apiSession().getLines( *fileId_, firstLine, number,
                                                        klogg::api::AnsiMode::Raw );
        if ( result ) {
            lines.reserve( result->size() );
            for ( auto& line : *result ) {
                lines.push_back( std::move( line.text ) );
            }
        }
        else if ( !isTransientApiError( result.error().code ) ) {
            LOG_WARNING << "LogMainView: getLines failed: " << result.error().message;
        }
    }

    // The API clamps out-of-range tails where the native getter pads; pad
    // with empty lines so callers indexing by `number` stay safe
    while ( lines.size() < number.get() ) {
        lines.push_back( std::string{} );
    }
    return lines;
}

LineNumber LogMainView::getLineNumber( LineNumber index ) const
{
    // Identity mapping, 1-based like AbstractLogData::getLineNumber
    return index + 1_lcount;
}

LinesCount LogMainView::getNbLine() const
{
    return cachedNbLines_;
}

LineLength LogMainView::getMaxLength() const
{
    return cachedMaxLineLength_;
}

LineLength LogMainView::getLineLength( LineNumber line ) const
{
    return LineLength{ static_cast<LineLength::UnderlyingType>(
        getExpandedLineString( line ).size() ) };
}

const QTextCodec* LogMainView::displayEncoding() const
{
    if ( fileId_ ) {
        const auto info = klogg::ui::apiSession().fileInfo( *fileId_ );
        if ( info ) {
            if ( auto* codec = QTextCodec::codecForName( info->encoding.name.c_str() ) ) {
                return codec;
            }
        }
        else if ( !isTransientApiError( info.error().code ) ) {
            LOG_WARNING << "LogMainView: fileInfo failed: " << info.error().message;
        }
    }
    return nullptr;
}

// Just update our internal record.
void LogMainView::useNewFiltering( std::optional<klogg::api::SearchId> searchId )
{
    searchId_ = searchId;

    if ( getOverview() != nullptr )
        getOverview()->setSource( fileId_, searchId_ );

    forceRefresh();
}

AbstractLogData::LineType LogMainView::lineType( LineNumber lineNumber ) const
{
    if ( const auto* cached = cachedGutterFlags( lineNumber ) ) {
        return lineTypeFromGutterFlags( cached->flagBits );
    }

    if ( fileId_ ) {
        // Gutter flags through the API: the mark bit comes from the
        // session-level marks, the match bit from the bound search (nullopt
        // yields marks only, e.g. before the first search), the event bit
        // from the file's latest completed extraction
        const auto flags
            = klogg::ui::apiSession().lineFlags( *fileId_, searchId_, lineNumber, 1_lcount );
        if ( flags ) {
            // Empty before the index has any lines: plain gutter, no log
            if ( !flags->empty() ) {
                return lineTypeFromGutterFlags( flags->front() );
            }
        }
        else if ( !isTransientApiError( flags.error().code ) ) {
            LOG_WARNING << "LogMainView: lineFlags failed: " << flags.error().message;
        }
    }

    return AbstractLogData::LineTypeFlags::Plain;
}

void LogMainView::prefetchGutterFlags( LineNumber firstLine, LinesCount count )
{
    if ( !fileId_ ) {
        AbstractLogView::prefetchGutterFlags( firstLine, count );
        return;
    }

    // The main view's lines are contiguous: one range query covers the frame
    const auto flags = klogg::ui::apiSession().lineFlags( *fileId_, searchId_, firstLine, count );
    if ( !flags ) {
        if ( !isTransientApiError( flags.error().code ) ) {
            LOG_WARNING << "LogMainView: lineFlags prefetch failed, per-line fallback: "
                        << flags.error().message;
        }
        AbstractLogView::prefetchGutterFlags( firstLine, count );
        return;
    }

    klogg::vector<GutterLineFlags> cache;
    cache.reserve( flags->size() );
    for ( size_t i = 0; i < flags->size(); ++i ) {
        cache.push_back( GutterLineFlags{ LineNumber{ firstLine.get() + i }, ( *flags )[ i ] } );
    }
    setGutterFlagsCache( firstLine, std::move( cache ) );
}

std::optional<klogg::api::ViewportResult> LogMainView::renderFrame( LineNumber firstLine,
                                                                    LinesCount count )
{
    if ( !fileId_ ) {
        return std::nullopt;
    }

    auto frame = klogg::ui::apiSession().renderViewport( *fileId_, searchId_, firstLine, count,
                                                         viewportOptions( firstLine, count ) );
    if ( !frame ) {
        if ( !isTransientApiError( frame.error().code ) ) {
            LOG_WARNING << "LogMainView: renderViewport failed, composing per line: "
                        << frame.error().message;
        }
        return std::nullopt;
    }

    // The paint loop indexes the frame by its own row count
    if ( frame->lines.size() != static_cast<size_t>( count.get() ) ) {
        LOG_WARNING << "LogMainView: renderViewport returned " << frame->lines.size()
                    << " lines for a frame of " << count.get();
        return std::nullopt;
    }

    return *frame;
}

std::optional<double> LogMainView::sessionScrollMultiplier() const
{
    if ( !fileId_ ) {
        return std::nullopt;
    }

    const auto multiplier = klogg::ui::apiSession().scrollMultiplier( *fileId_ );
    if ( !multiplier ) {
        if ( !isTransientApiError( multiplier.error().code ) ) {
            LOG_WARNING << "LogMainView: scrollMultiplier failed: " << multiplier.error().message;
        }
        return std::nullopt;
    }
    return *multiplier;
}

std::optional<int64_t> LogMainView::sessionScrollPosToLine( int64_t scrollPos ) const
{
    if ( !fileId_ ) {
        return std::nullopt;
    }

    const auto line = klogg::ui::apiSession().scrollPosToLine( *fileId_, scrollPos );
    if ( !line ) {
        if ( !isTransientApiError( line.error().code ) ) {
            LOG_WARNING << "LogMainView: scrollPosToLine failed: " << line.error().message;
        }
        return std::nullopt;
    }
    return static_cast<int64_t>( line->get() );
}

std::optional<int64_t> LogMainView::sessionLineToScrollPos( LineNumber line ) const
{
    if ( !fileId_ ) {
        return std::nullopt;
    }

    const auto scrollPos = klogg::ui::apiSession().lineToScrollPos( *fileId_, line );
    if ( !scrollPos ) {
        if ( !isTransientApiError( scrollPos.error().code ) ) {
            LOG_WARNING << "LogMainView: lineToScrollPos failed: " << scrollPos.error().message;
        }
        return std::nullopt;
    }
    return *scrollPos;
}

std::optional<LineNumber> LogMainView::markAfter( LineNumber line ) const
{
    if ( fileId_ ) {
        const auto result = klogg::ui::apiSession().markAfter( *fileId_, line );
        if ( result ) {
            return *result;
        }
        if ( !isTransientApiError( result.error().code ) ) {
            LOG_WARNING << "LogMainView: markAfter failed: " << result.error().message;
        }
    }
    return std::nullopt;
}

std::optional<LineNumber> LogMainView::markBefore( LineNumber line ) const
{
    if ( fileId_ ) {
        const auto result = klogg::ui::apiSession().markBefore( *fileId_, line );
        if ( result ) {
            return *result;
        }
        if ( !isTransientApiError( result.error().code ) ) {
            LOG_WARNING << "LogMainView: markBefore failed: " << result.error().message;
        }
    }
    return std::nullopt;
}

void LogMainView::doRegisterShortcuts()
{
    LOG_INFO << "Registering shortcuts for main view";
    AbstractLogView::doRegisterShortcuts();
    registerShortcut( ShortcutAction::LogViewNextMark, [ this ] {
        const auto line = markAfter( getViewPosition() );
        if ( line.has_value() ) {
            selectAndDisplayLine( *line );
        }
    } );
    registerShortcut( ShortcutAction::LogViewPrevMark, [ this ] {
        const auto line = markBefore( getViewPosition() );
        if ( line.has_value() ) {
            selectAndDisplayLine( *line );
        }
    } );
}
