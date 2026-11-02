/*
 * Copyright (C) 2009, 2010, 2012, 2017 Nicolas Bonnefon and other contributors
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

// This file implements the FilteredView concrete class.
// Most of the actual drawing and event management is done in AbstractLogView
// Only behaviour specific to the filtered (bottom) view is implemented here.

#include <algorithm>
#include <cassert>

#include <QTextCodec>
#include <QThread>

#include "filteredview.h"
#include "shortcuts.h"

#include "apisession.h"
#include "klogg/api_session.h"
#include "log.h"
#include "log_qt.h"

FilteredView::FilteredView( const QuickFindPattern* const quickFindPattern,
                            std::optional<klogg::api::FileId> fileId, QWidget* parent )
    : AbstractLogView( quickFindPattern, parent )
    , fileId_( fileId )
{
}

void FilteredView::setVisibility( Visibility visi )
{
    // A live search switches visibility through the API; the search-less
    // state renders the empty projection pushed by the CrawlerWidget
    if ( searchId_ ) {
        using VisibilityFlags = LogFilteredData::VisibilityFlags;
        const auto unionCount = klogg::ui::apiSession().setSearchVisibility(
            *searchId_, visi.testFlag( VisibilityFlags::Matches ),
            visi.testFlag( VisibilityFlags::Marks ), visi.testFlag( VisibilityFlags::Events ) );
        if ( !unionCount ) {
            LOG_WARNING << "FilteredView: setSearchVisibility failed: "
                        << unionCount.error().message;
        }
    }

    updateData();
}

void FilteredView::useNewFilteredData( std::optional<klogg::api::SearchId> searchId )
{
    // The QuickFind worker resolves lines through this view from its own
    // thread (B6): re-create it for the new data binding
    restartQuickFind();

    searchId_ = searchId;
    marksCache_.reset();
    // The binding changed: the getLines frame stash no longer maps this view
    lastGetLinesSourceLines_.clear();

    updateData();
}

void FilteredView::setEmptyProjection( std::vector<LineNumber> lines )
{
    emptyProjectionLines_ = std::move( lines );
    emptyProjectionActive_ = true;
    emptyProjectionMaxLengthComputed_ = false;

    updateData();
}

LineNumber FilteredView::emptyProjectionSourceLine( LineNumber index ) const
{
    if ( index.get() < emptyProjectionLines_.size() ) {
        return emptyProjectionLines_[ index.get() ];
    }
    return maxValue<LineNumber>();
}

// Gutter bullet flags for a projection index (all bits via the API)
AbstractLogData::LineType FilteredView::lineType( LineNumber lineNumber ) const
{
    if ( const auto* cached = cachedGutterFlags( lineNumber ) ) {
        return lineTypeFromGutterFlags( cached->flagBits );
    }

    if ( fileId_ && searchId_ ) {
        // The view line is an index into the search's union projection: map it
        // to the source line, then pull the gutter flags for that line
        const auto lines = klogg::ui::apiSession().getFilteredLines( *searchId_, lineNumber.get(),
                                                                     1, klogg::api::AnsiMode::Raw );
        if ( lines && !lines->empty() ) {
            const auto line = lines->front().num;
            const auto flags
                = klogg::ui::apiSession().lineFlags( *fileId_, *searchId_, line, 1_lcount );
            if ( flags && !flags->empty() ) {
                return lineTypeFromGutterFlags( flags->front() );
            }
        }

        LOG_WARNING << "FilteredView: line flags query failed, reporting a plain line";
    }

    if ( fileId_ && hasEmptyProjection() ) {
        // Search-less projection: marks and event bits of the mapped source
        // line, no match bit without a search
        const auto sourceLine = emptyProjectionSourceLine( lineNumber );
        if ( sourceLine != maxValue<LineNumber>() ) {
            const auto flags
                = klogg::ui::apiSession().lineFlags( *fileId_, std::nullopt, sourceLine, 1_lcount );
            if ( flags && !flags->empty() ) {
                return lineTypeFromGutterFlags( flags->front() );
            }
        }

        LOG_WARNING << "FilteredView: empty projection flags query failed";
    }

    return AbstractLogData::LineTypeFlags::Plain;
}

void FilteredView::prefetchGutterFlags( LineNumber firstLine, LinesCount count )
{
    if ( !fileId_ || ( !searchId_ && !hasEmptyProjection() ) ) {
        AbstractLogView::prefetchGutterFlags( firstLine, count );
        return;
    }

    // The view lines are a non-contiguous set of source lines: map the frame
    // through the projection, then fetch all flags in one batch call. The
    // frame's getLines just fetched this range, so its source line numbers
    // are reused instead of fetching the lines again.
    std::vector<LineNumber> sourceLines;
    if ( lastGetLinesStart_ == firstLine && !lastGetLinesSourceLines_.empty() ) {
        const auto kept = std::min<std::size_t>( count.get(), lastGetLinesSourceLines_.size() );
        sourceLines.assign( lastGetLinesSourceLines_.begin(),
                            lastGetLinesSourceLines_.begin()
                                + static_cast<std::ptrdiff_t>( kept ) );
    }
    else if ( searchId_ ) {
        const auto lines = klogg::ui::apiSession().getFilteredLines(
            *searchId_, firstLine.get(), static_cast<uint32_t>( count.get() ),
            klogg::api::AnsiMode::Raw );
        if ( !lines ) {
            LOG_WARNING << "FilteredView: projection prefetch failed, per-line fallback: "
                        << lines.error().message;
            AbstractLogView::prefetchGutterFlags( firstLine, count );
            return;
        }
        sourceLines.reserve( lines->size() );
        for ( const auto& line : *lines ) {
            sourceLines.push_back( line.num );
        }
    }
    else {
        // Search-less projection: the frame maps to a slice of the sorted
        // source lines
        const auto first = static_cast<std::size_t>( firstLine.get() );
        const auto available = first < emptyProjectionLines_.size()
                                   ? emptyProjectionLines_.size() - first
                                   : std::size_t{ 0 };
        const auto kept = std::min<std::size_t>( count.get(), available );
        const auto sliceStart
            = emptyProjectionLines_.begin() + static_cast<std::ptrdiff_t>( first );
        sourceLines.assign( sliceStart, sliceStart + static_cast<std::ptrdiff_t>( kept ) );
    }

    // nullopt search id covers the search-less projection (marks+events bits)
    const auto flags = klogg::ui::apiSession().lineFlagsBatch( *fileId_, searchId_, sourceLines );
    if ( !flags || flags->size() != sourceLines.size() ) {
        LOG_WARNING << "FilteredView: lineFlagsBatch prefetch failed, per-line fallback";
        AbstractLogView::prefetchGutterFlags( firstLine, count );
        return;
    }

    klogg::vector<GutterLineFlags> cache;
    cache.reserve( flags->size() );
    for ( size_t i = 0; i < flags->size(); ++i ) {
        cache.push_back( GutterLineFlags{ sourceLines[ i ], ( *flags )[ i ] } );
    }
    setGutterFlagsCache( firstLine, std::move( cache ) );
}

LineNumber FilteredView::displayLineNumber( LineNumber lineNumber ) const
{
    // The frame cache carries the same projection (B5c): the paint loop's
    // per-line queries are served without a session round-trip
    if ( const auto* cached = cachedGutterFlags( lineNumber ) ) {
        return cached->sourceLine + 1_lcount; // show a 1-based index
    }

    // The getLineNumber override maps a projection index to the 1-based
    // source line, through the API when a search is bound
    return getLineNumber( lineNumber );
}

LineNumber FilteredView::lineIndex( LineNumber lineNumber ) const
{
    if ( searchId_ ) {
        // Floor over the union projection (B8-3a): the greatest member index
        // at or before the source line; a line preceding every member clamps
        // to 0 like the native getLineIndexNumber did
        const auto index
            = klogg::ui::apiSession().filteredLineIndexAtOrBefore( *searchId_, lineNumber );
        if ( index ) {
            return LineNumber( static_cast<LineNumber::UnderlyingType>( index->value_or( 0 ) ) );
        }

        LOG_WARNING << "FilteredView: filteredLineIndexAtOrBefore failed, "
                       "falling back to the local projection floor: "
                    << index.error().message;
    }

    if ( hasEmptyProjection() ) {
        // Local floor over the sorted projection, same clamp at 0
        const auto it = std::upper_bound( emptyProjectionLines_.cbegin(),
                                          emptyProjectionLines_.cend(), lineNumber );
        if ( it == emptyProjectionLines_.cbegin() ) {
            return 0_lnum;
        }
        return LineNumber( static_cast<LineNumber::UnderlyingType>(
            std::distance( emptyProjectionLines_.cbegin(), it - 1 ) ) );
    }

    return 0_lnum;
}

LineNumber FilteredView::maxDisplayLineNumber() const
{
    if ( fileId_ ) {
        const auto info = klogg::ui::apiSession().fileInfo( *fileId_ );
        if ( info ) {
            return LineNumber( info->total_lines.get() );
        }
        LOG_WARNING << "FilteredView: fileInfo failed: " << info.error().message;
    }
    return LineNumber( getNbLine().get() );
}

std::string FilteredView::getLineString( LineNumber line ) const
{
    const auto lines = getLines( line, 1_lcount );
    return lines.empty() ? std::string{} : lines.front();
}

std::string FilteredView::getExpandedLineString( LineNumber line ) const
{
    return untabify( getLineString( line ) );
}

klogg::vector<std::string> FilteredView::getLines( LineNumber firstLine,
                                                   LinesCount number ) const
{
    if ( searchId_ ) {
        // Mutable so the loop below can move the text out of the returned
        // lines instead of copying it into the paint buffer
        auto result = klogg::ui::apiSession().getFilteredLines(
            *searchId_, firstLine.get(), static_cast<uint32_t>( number.get() ),
            klogg::api::AnsiMode::Raw );
        if ( result ) {
            // The source-line stash feeds the same frame's gutter prefetch
            // (B5c) and the range-copy line-number mapping in getLineNumber;
            // it is paint-loop state: the QuickFind worker fetches through
            // this override from its own thread (B6) and must not touch it
            const bool isGuiThread = ( QThread::currentThread() == thread() );

            klogg::vector<std::string> lines;
            lines.reserve( result->size() );
            if ( isGuiThread ) {
                lastGetLinesSourceLines_.clear();
                lastGetLinesSourceLines_.reserve( result->size() );
            }
            for ( auto& line : *result ) {
                lines.push_back( std::move( line.text ) );
                if ( isGuiThread ) {
                    lastGetLinesSourceLines_.push_back( line.num );
                }
            }
            if ( isGuiThread ) {
                lastGetLinesStart_ = firstLine;
            }
            // The API clamps out-of-range tails where the native getter pads;
            // pad with empty lines so callers indexing by `number` stay safe
            while ( lines.size() < number.get() ) {
                lines.push_back( std::string{} );
            }
            return lines;
        }

        if ( QThread::currentThread() == thread() ) {
            lastGetLinesSourceLines_.clear();
        }
        LOG_WARNING << "FilteredView: getFilteredLines failed: " << result.error().message;
    }

    if ( hasEmptyProjection() && fileId_ ) {
        // Map the frame through the sorted projection, then one batch fetch
        // covers the non-contiguous source lines
        std::vector<LineNumber> sourceLines;
        const auto first = static_cast<std::size_t>( firstLine.get() );
        const auto available = first < emptyProjectionLines_.size()
                                   ? emptyProjectionLines_.size() - first
                                   : std::size_t{ 0 };
        const auto kept = std::min<std::size_t>( number.get(), available );
        const auto sliceStart
            = emptyProjectionLines_.begin() + static_cast<std::ptrdiff_t>( first );
        sourceLines.assign( sliceStart, sliceStart + static_cast<std::ptrdiff_t>( kept ) );

        auto result = klogg::ui::apiSession().getLinesBatch( *fileId_, sourceLines );
        if ( result ) {
            // Same paint-loop stash rules as the search path above (B5c/B6)
            const bool isGuiThread = ( QThread::currentThread() == thread() );

            klogg::vector<std::string> lines;
            lines.reserve( result->size() );
            if ( isGuiThread ) {
                lastGetLinesSourceLines_.clear();
                lastGetLinesSourceLines_.reserve( result->size() );
            }
            for ( auto& line : *result ) {
                lines.push_back( std::move( line.text ) );
                if ( isGuiThread ) {
                    lastGetLinesSourceLines_.push_back( line.num );
                }
            }
            if ( isGuiThread ) {
                lastGetLinesStart_ = firstLine;
            }
            while ( lines.size() < number.get() ) {
                lines.push_back( std::string{} );
            }
            return lines;
        }

        if ( QThread::currentThread() == thread() ) {
            lastGetLinesSourceLines_.clear();
        }
        LOG_WARNING << "FilteredView: getLinesBatch failed: " << result.error().message;
    }

    // Pad with empty lines so callers indexing by `number` stay safe
    klogg::vector<std::string> emptyLines;
    while ( emptyLines.size() < number.get() ) {
        emptyLines.push_back( std::string{} );
    }
    return emptyLines;
}

LineNumber FilteredView::getLineNumber( LineNumber index ) const
{
    if ( searchId_ ) {
        // A range copy pulls its lines in one getLines batch right before
        // walking the per-line numbers; serve hits from that frame's
        // source-line stash instead of one session round-trip per line.
        // The stash is GUI-thread paint state (B6): the QuickFind worker
        // resolves from its own thread and always takes the session path
        if ( QThread::currentThread() == thread() && index >= lastGetLinesStart_
             && index.get() - lastGetLinesStart_.get() < lastGetLinesSourceLines_.size() ) {
            return lastGetLinesSourceLines_[ index.get() - lastGetLinesStart_.get() ] + 1_lcount;
        }

        // getFilteredLines is the vehicle for the index->line mapping (not
        // searchMatches): its num field maps any index of the union
        // projection, while searchMatches is bounded by the pure-match count
        // and returns nothing for union indices past it (B5a coupling note).
        // The public contract is 1-based like AbstractLogData::getLineNumber.
        const auto result = klogg::ui::apiSession().getFilteredLines( *searchId_, index.get(), 1,
                                                                      klogg::api::AnsiMode::Raw );
        if ( result && !result->empty() ) {
            return result->front().num + 1_lcount;
        }
        if ( !result ) {
            LOG_WARNING << "FilteredView: getFilteredLines failed: " << result.error().message;
        }
    }

    if ( hasEmptyProjection() ) {
        // The public contract is 1-based like AbstractLogData::getLineNumber
        const auto sourceLine = emptyProjectionSourceLine( index );
        return sourceLine != maxValue<LineNumber>() ? sourceLine + 1_lcount
                                                    : maxValue<LineNumber>();
    }

    return AbstractLogView::getLineNumber( index );
}

LinesCount FilteredView::getNbLine() const
{
    if ( searchId_ ) {
        const auto result = klogg::ui::apiSession().getFilteredLineCount( *searchId_ );
        if ( result ) {
            return *result;
        }

        LOG_WARNING << "FilteredView: getFilteredLineCount failed: " << result.error().message;
    }

    if ( hasEmptyProjection() ) {
        return LinesCount(
            static_cast<LinesCount::UnderlyingType>( emptyProjectionLines_.size() ) );
    }

    return 0_lcount;
}

LineLength FilteredView::getMaxLength() const
{
    if ( searchId_ ) {
        // The view needs the exact filtered-set max for the horizontal
        // scroll range (updateScrollBars)
        const auto maxLength = klogg::ui::apiSession().filteredMaxLineLength( *searchId_ );
        if ( maxLength ) {
            return *maxLength;
        }

        LOG_WARNING << "FilteredView: filteredMaxLineLength failed: " << maxLength.error().message;
    }

    if ( hasEmptyProjection() ) {
        if ( !emptyProjectionMaxLengthComputed_ ) {
            // The scroll range needs the exact max over the projected lines;
            // computed once per projection push
            emptyProjectionMaxLengthComputed_ = true;
            emptyProjectionMaxLength_ = 0_length;
            if ( fileId_ && !emptyProjectionLines_.empty() ) {
                const auto batch
                    = klogg::ui::apiSession().getLinesBatch( *fileId_, emptyProjectionLines_ );
                if ( batch ) {
                    for ( const auto& line : *batch ) {
                        const auto expanded = untabify( QString::fromStdString( line.text ) );
                        emptyProjectionMaxLength_
                            = qMax( emptyProjectionMaxLength_, LineLength{ expanded.size() } );
                    }
                }
                else {
                    LOG_WARNING << "FilteredView: getLinesBatch failed: " << batch.error().message;
                }
            }
        }
        return emptyProjectionMaxLength_;
    }

    return 0_length;
}

LineLength FilteredView::getLineLength( LineNumber line ) const
{
    if ( searchId_ || hasEmptyProjection() ) {
        // Same tab-expanded size the native chain computes
        // (LogFilteredData delegates to LogData::getLineLength)
        return LineLength{ static_cast<LineLength::UnderlyingType>( getExpandedLineString( line ).size() ) };
    }

    return 0_length;
}

const QTextCodec* FilteredView::displayEncoding() const
{
    if ( fileId_ ) {
        const auto info = klogg::ui::apiSession().fileInfo( *fileId_ );
        if ( info ) {
            if ( auto* codec = QTextCodec::codecForName( info->encoding.name.c_str() ) ) {
                return codec;
            }
        }
        else {
            LOG_WARNING << "FilteredView: fileInfo failed: " << info.error().message;
        }
    }
    return nullptr;
}

void FilteredView::doRegisterShortcuts()
{
    LOG_INFO << "Registering shortcuts for filtered view";
    AbstractLogView::doRegisterShortcuts();
    // Same mapping as the main view: NextMark moves towards the file end,
    // PrevMark towards the file start
    registerShortcut( ShortcutAction::LogViewNextMark, [ this ] { selectFilteredMark( false ); } );
    registerShortcut( ShortcutAction::LogViewPrevMark, [ this ] { selectFilteredMark( true ); } );
}

void FilteredView::invalidateMarksCache()
{
    marksCache_.reset();
}

void FilteredView::selectFilteredMark( bool backward )
{
    if ( fileId_ ) {
        selectFilteredMarkApi( backward );
    }
}

bool FilteredView::selectFilteredMarkApi( bool backward )
{
    auto& session = klogg::ui::apiSession();
    if ( !marksCache_ ) {
        const auto marks = session.listMarks( *fileId_ );
        if ( !marks ) {
            LOG_WARNING << "FilteredView: listMarks failed: " << marks.error().message;
            return false;
        }
        // Session marks are stored in an ordered set
        marksCache_ = std::vector<LineNumber>( marks->begin(), marks->end() );
    }
    const auto& marks = *marksCache_;
    if ( marks.empty() ) {
        return true;
    }

    // The current position as a source line: the search-less projection's
    // view position indexes its sorted lines directly; a bound search maps
    // through its union projection
    const auto position = getViewPosition();
    LineNumber currentSourceLine;
    if ( searchId_ ) {
        const auto lines
            = session.getFilteredLines( *searchId_, position.get(), 1, klogg::api::AnsiMode::Raw );
        if ( !lines || lines->empty() ) {
            LOG_WARNING << "FilteredView: position mapping failed";
            return false;
        }
        currentSourceLine = lines->front().num;
    }
    else {
        if ( !hasEmptyProjection() || position.get() >= emptyProjectionLines_.size() ) {
            return false;
        }
        currentSourceLine = emptyProjectionLines_[ position.get() ];
    }

    // Marks and projection indices order identically, so the neighbour search
    // runs on source lines
    const auto candidate
        = backward ? std::lower_bound( marks.cbegin(), marks.cend(), currentSourceLine )
                   : std::upper_bound( marks.cbegin(), marks.cend(), currentSourceLine );
    if ( backward ? candidate == marks.cbegin() : candidate == marks.cend() ) {
        return true;
    }
    const auto markIt = backward ? candidate - 1 : candidate;

    uint64_t index;
    if ( searchId_ ) {
        const auto projectionIndex = session.filteredLineIndex( *searchId_, *markIt );
        if ( !projectionIndex ) {
            LOG_WARNING << "FilteredView: filteredLineIndex failed: "
                        << projectionIndex.error().message;
            return false;
        }
        // A mark outside the current visibility union has no index to select
        if ( !projectionIndex->has_value() ) {
            return true;
        }
        index = **projectionIndex;
    }
    else {
        // The search-less projection may filter the marks out (visibility
        // without the Marks class)
        const auto projectionIt = std::lower_bound( emptyProjectionLines_.cbegin(),
                                                    emptyProjectionLines_.cend(), *markIt );
        if ( projectionIt == emptyProjectionLines_.cend() || *projectionIt != *markIt ) {
            return true;
        }
        index = static_cast<uint64_t>(
            std::distance( emptyProjectionLines_.cbegin(), projectionIt ) );
    }

    selectAndDisplayLine( LineNumber( static_cast<LineNumber::UnderlyingType>( index ) ) );
    return true;
}
