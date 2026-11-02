/*
 * Copyright (C) 2011, 2012 Nicolas Bonnefon
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

// This file implements the Overview class.
// It provides support for drawing the match overview sidebar but
// the actual drawing is done in AbstractLogView which uses this class.
//
// The projection pull (union of matches/marks/events of the bound search)
// costs one API round-trip per 4096 lines and is the dominant GUI stall of
// the B8 view layer, so data fetching and height-dependent bucketing are
// separated: fetching runs asynchronously on a pool thread and is throttled,
// while a pure resize only re-buckets the cached source lines without any
// API call.

#include <cmath>

#include <QMetaObject>
#include <QWidget>

#include <QtConcurrent>

#include "linetypes_qt.h"
#include "log.h"
#include "log_qt.h"

#include "apisession.h"
#include "klogg/api_session.h"

#include "overview.h"
#include "uitracing.h"

namespace {

// The whole union projection is pulled in pages; a page bounds one session
// round-trip's worth of lines
constexpr uint32_t kOverviewPageLines = 4096;

// Min interval between projection fetches while a search is in progress; a
// settled (100%) search bypasses the throttle so the final state shows at once
constexpr auto kMinFetchInterval = std::chrono::milliseconds{ 500 };

} // namespace

Overview::Overview()
    : matchLines_()
    , markLines_()
{
    height_ = 0;
    dirty_ = true;
    visible_ = false;
}

void Overview::setSource( std::optional<klogg::api::FileId> fileId,
                          std::optional<klogg::api::SearchId> searchId )
{
    LOG_INFO << "OverviewWidget::setSource";

    fileId_ = fileId;
    searchId_ = searchId;
    dirty_ = true;
    projectionChanged_ = true;
    // Any in-flight fetch is for the previous binding; its delivery is
    // dropped by the generation check
    ++fetchGeneration_;
}

void Overview::setEmptyProjection( std::vector<LineNumber> lines )
{
    emptyProjectionLines_ = std::move( lines );
    dirty_ = true;
    projectionChanged_ = true;
}

void Overview::updateData( LinesCount totalNbLine )
{
    LOG_INFO << "OverviewWidget::updateData " << totalNbLine;

    linesInFile_ = totalNbLine;
    dirty_ = true;
}

void Overview::updateView( unsigned height )
{
    KLOGG_UI_TRACE_SCOPE( "Overview::updateView" );

    const bool heightChanged = ( height != height_ );
    height_ = height;

    if ( dirty_ ) {
        if ( searchId_ && fileId_ ) {
            // Cheap staleness probe: one small round-trip tells whether the
            // engine-side state moved since the cached fetch; a repaint after
            // e.g. a tab switch then costs no refetch at all
            const auto progress = klogg::ui::apiSession().searchProgress( *searchId_ );
            const auto progressPercent
                = progress ? static_cast<int>( std::lround( progress->progress * 100.0f ) )
                           : fetchedProgressPercent_;

            if ( !projectionChanged_ && fetchedProgressPercent_ >= 0 && progress
                 && progress->total_matches.get() == fetchedMatches_
                 && progressPercent == fetchedProgressPercent_ ) {
                dirty_ = false;
            }
            else if ( !fetchInFlight_ ) {
                const auto now = std::chrono::steady_clock::now();
                const bool settled = progress && progress->progress >= 1.0f;
                if ( settled || ( now - lastFetchStart_ ) >= kMinFetchInterval ) {
                    startAsyncFetch( progress ? progress->total_matches.get() : 0,
                                     progressPercent );
                }
            }
        }
        else {
            // Search-less projection is pushed wholesale; no fetch needed
            dirty_ = false;
            bucketsDirty_ = true;
        }
    }

    // A pending fetch never blocks the repaint: re-bucket the (possibly
    // stale) cache so a pure resize stays API-free
    if ( bucketsDirty_ || heightChanged ) {
        rebucket();
    }
}

const klogg::vector<Overview::WeightedLine>* Overview::getMatchLines() const
{
    return &matchLines_;
}

const klogg::vector<Overview::WeightedLine>* Overview::getMarkLines() const
{
    return &markLines_;
}

std::pair<int, int> Overview::getViewLines() const
{
    int top = 0;
    int bottom = static_cast<int>( height_ ) - 1;

    if ( linesInFile_.get() > 0 ) {
        top = static_cast<int>( ( topLine_.get() ) * height_ / ( linesInFile_.get() ) );

        bottom = top + static_cast<int>( nbLines_.get() * height_ / ( linesInFile_.get() ) );
    }

    return std::make_pair( top, bottom );
}

LineNumber Overview::fileLineFromY( int position ) const
{
    const auto line = static_cast<LineNumber::UnderlyingType>(
        static_cast<LineNumber::UnderlyingType>( position ) * linesInFile_.get() / static_cast<LineNumber::UnderlyingType>( height_ ) );

    return LineNumber{ line };
}

int Overview::yFromFileLine( LineNumber fileLine ) const
{
    int position = 0;

    if ( linesInFile_.get() > 0 )
        position = static_cast<int>( fileLine.get() * height_ / linesInFile_.get() );

    return position;
}

// Rebuild the weighted buckets from the cached source lines
void Overview::rebucket()
{
    KLOGG_UI_TRACE_SCOPE( "Overview::rebucket" );
    LOG_DEBUG << "Overview::rebucket";

    matchLines_.clear();
    markLines_.clear();

    if ( linesInFile_.get() > 0 && height_ > 0 ) {
        if ( searchId_ ) {
            for ( size_t i = 0; i < source_.lines.size(); ++i ) {
                addLine( source_.lines[ i ], source_.matchBits[ i ] != 0 );
            }
        }
        else {
            // Search-less projection (marks union event lines) pushed by the
            // owning CrawlerWidget; without a search no line carries the
            // Match bit, so everything buckets as a mark line
            for ( const auto line : emptyProjectionLines_ ) {
                addLine( line, false );
            }
        }
    }

    bucketsDirty_ = false;
}

void Overview::addLine( LineNumber line, bool isMatch )
{
    // Same bucketing as the native iteration had: match bit set -> match
    // line, anything else (marks, events) -> mark line
    const auto position = yFromFileLine( line );
    auto& lines = isMatch ? matchLines_ : markLines_;
    if ( ( !lines.empty() ) && lines.back().position() == position ) {
        // If the line is already there, we increase its weight
        lines.back().load();
    }
    else {
        // If not we just add it
        lines.emplace_back( position );
    }
}

void Overview::startAsyncFetch( uint64_t totalMatches, int progressPercent )
{
    fetchInFlight_ = true;
    lastFetchStart_ = std::chrono::steady_clock::now();
    fetchedMatches_ = totalMatches;
    fetchedProgressPercent_ = progressPercent;
    projectionChanged_ = false;

    const auto generation = ++fetchGeneration_;
    const auto fileId = *fileId_;
    const auto searchId = *searchId_;
    const QPointer<QObject> context = deliveryContext_;

    // The returned QFuture is intentionally not watched: delivery is via the
    // invokeMethod inside the task; destroying the future does not cancel it
    const auto fetchTask = QtConcurrent::run( [ fileId, searchId, generation, context, this ] {
        auto data = fetchSourceFromApi( fileId, searchId );
        if ( !context ) {
            return;
        }
        QMetaObject::invokeMethod(
            context,
            [ context, this, generation, data = std::move( data ) ]() mutable {
                if ( !context ) {
                    // The widget (and with it this Overview) is gone
                    return;
                }
                if ( data ) {
                    adoptFetchedSource( generation, std::move( *data ) );
                }
                else {
                    // Fetch failed: keep the stale cache, retry on next paint.
                    // A setSource may have started a newer fetch meanwhile;
                    // only release the in-flight state this generation owns
                    if ( generation == fetchGeneration_ ) {
                        fetchInFlight_ = false;
                        dirty_ = true;
                    }
                }
                static_cast<QWidget*>( context.data() )->update();
            },
            Qt::QueuedConnection );
    } );
    Q_UNUSED( fetchTask );
}

void Overview::adoptFetchedSource( uint64_t generation, SourceData data )
{
    if ( generation != fetchGeneration_ ) {
        // The binding changed while the fetch was in flight; drop stale data
        // without touching the in-flight state owned by the newer fetch
        return;
    }
    fetchInFlight_ = false;

    source_ = std::move( data );
    dirty_ = false;
    bucketsDirty_ = true;
    rebucket();
}

std::optional<Overview::SourceData>
Overview::fetchSourceFromApi( klogg::api::FileId fileId, klogg::api::SearchId searchId )
{
    KLOGG_UI_TRACE_SCOPE( "Overview::fetchSourceFromApi" );

    auto& session = klogg::ui::apiSession();
    SourceData result;

    for ( uint64_t from = 0;; ) {
        // Text-less projection pull: only the line numbers are needed; the
        // match/mark bucketing falls out of the flags batch
        const auto page = session.filteredLineNumbers( searchId, from, kOverviewPageLines );
        if ( !page ) {
            LOG_WARNING << "Overview: filteredLineNumbers failed: " << page.error().message;
            return std::nullopt;
        }
        if ( page->empty() ) {
            return result;
        }

        const auto flags = session.lineFlagsBatch( fileId, searchId, *page );
        if ( !flags || flags->size() != page->size() ) {
            LOG_WARNING << "Overview: lineFlagsBatch failed";
            return std::nullopt;
        }
        for ( size_t i = 0; i < page->size(); ++i ) {
            result.lines.push_back( ( *page )[ i ] );
            result.matchBits.push_back(
                static_cast<uint8_t>( ( *flags )[ i ] & klogg::api::LineFlagMatch ) );
        }

        from += page->size();
        if ( page->size() < kOverviewPageLines ) {
            return result;
        }
    }
}
