/*
 * Copyright (C) 2011, 2012 Nicolas Bonnefon and other contributors
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

#ifndef OVERVIEW_H
#define OVERVIEW_H

#include "linetypes_qt.h"
#include <QList>
#include <QVector>

#include <chrono>
#include <optional>
#include <vector>

#include <QPointer>

#include "klogg/api_types.h"

// Class implementing the logic behind the matches overview bar.
// This class converts the matches found in a LogFilteredData in
// a screen dependent set of coloured lines, which is cached.
// This class is not a UI class, actual display is left to the client.
//
// This class is NOT thread-safe.
class Overview {
  public:
    // A line with a position in pixel and a weight (darkness)
    class WeightedLine {
      public:
        static constexpr int WEIGHT_STEPS = 3;

        WeightedLine()
        {
            pos_ = 0;
            weight_ = 0;
        }
        // (Necessary for QVector)
        explicit WeightedLine( int pos )
        {
            pos_ = pos;
            weight_ = 0;
        }

        int position() const
        {
            return pos_;
        }
        int weight() const
        {
            return weight_;
        }

        void load()
        {
            weight_ = qMin( weight_ + 1, WEIGHT_STEPS - 1 );
        }

      private:
        int pos_;
        int weight_;
    };

    Overview();

    // Bind the overview to a file/search pair of the Core API session
    void setSource( std::optional<klogg::api::FileId> fileId,
                    std::optional<klogg::api::SearchId> searchId );
    // Search-less projection pushed by the owning CrawlerWidget (marks union
    // the populated extraction's event lines); read when no search is bound
    void setEmptyProjection( std::vector<LineNumber> lines );
    // Signal the overview its bound source has been changed and the overview
    // must be updated with the provided total number of line of the file.
    void updateData( LinesCount totalNbLine );
    // Set the visibility flag of this overview.
    void setVisible( bool visible )
    {
        visible_ = visible;
        dirty_ = visible;
    }

    // Context object (the OverviewWidget) async fetch results are marshalled
    // back to; guarded by QPointer so a dead widget drops them
    void setDeliveryContext( QObject* context )
    {
        deliveryContext_ = context;
    }

    // True when fresh data exists but the fetch was deferred (throttled); the
    // widget should schedule a repaint to pick it up once the interval passes
    bool fetchPending() const
    {
        return dirty_ && searchId_.has_value() && !fetchInFlight_;
    }

    // Update the current position in the file (to draw the view line)
    void updateCurrentPosition( LineNumber firstLine, LineNumber lastLine )
    {
        topLine_ = firstLine;
        nbLines_ = LinesCount( lastLine.get() - firstLine.get() );
    }

    // Returns weither this overview is visible.
    bool isVisible()
    {
        return visible_;
    }
    // Signal the overview the height of the display has changed, triggering
    // an update of its cache.
    void updateView( unsigned height );
    // Returns a list of lines (between 0 and 'height') representing matches.
    // (pointer returned is valid until next call to update*()
    const klogg::vector<WeightedLine>* getMatchLines() const;
    // Returns a list of lines (between 0 and 'height') representing marks.
    // (pointer returned is valid until next call to update*()
    const klogg::vector<WeightedLine>* getMarkLines() const;
    // Return a pair of lines (between 0 and 'height') representing the current view.
    std::pair<int, int> getViewLines() const;

    // Return the line number corresponding to the passed overview y coordinate.
    LineNumber fileLineFromY( int y ) const;
    // Return the y coordinate corresponding to the passed line number.
    int yFromFileLine( LineNumber fileLine ) const;

private:
    // One projected source line and its match bit, fetched once per data
    // change and re-bucketed for free on pure height changes
    struct SourceData {
        std::vector<LineNumber> lines;
        std::vector<uint8_t> matchBits;
    };

    // Bound file/search of the Core API session
    std::optional<klogg::api::FileId> fileId_;
    std::optional<klogg::api::SearchId> searchId_;
    // Search-less projection pushed by the owning CrawlerWidget
    std::vector<LineNumber> emptyProjectionLines_;
    // Cached source projection of the bound search (GUI thread only)
    SourceData source_;
    // Total number of lines in the file.
    LinesCount linesInFile_;
    // Whether the overview is visible.
    bool visible_;
    // First and last line currently viewed.
    LineNumber topLine_;
    LinesCount nbLines_;
    // Current height of view window.
    unsigned height_;
    // Does the source data need a refetch (search progressed, marks changed).
    bool dirty_;
    // Do the weighted buckets need a rebuild from the cached source.
    bool bucketsDirty_ = true;
    // The pushed projection/search binding changed since the last fetch
    bool projectionChanged_ = false;

    // Async fetch state (GUI thread only): the pull runs on a pool thread so
    // paint never blocks on the API
    QPointer<QObject> deliveryContext_;
    bool fetchInFlight_ = false;
    uint64_t fetchGeneration_ = 0;
    std::chrono::steady_clock::time_point lastFetchStart_{};
    // Engine-side state the cache was fetched at (staleness key)
    uint64_t fetchedMatches_ = 0;
    int fetchedProgressPercent_ = -1;

    // List of lines representing matches and marks (are shared with the client)
    klogg::vector<WeightedLine> matchLines_;
    klogg::vector<WeightedLine> markLines_;

    // Rebuild the weighted buckets from the cached source for the current
    // height; pure CPU, no API calls
    void rebucket();
    // Buckets one projected line into the match or mark list
    void addLine( LineNumber line, bool isMatch );
    // Start an async pull of the bound search's union projection; the result
    // arrives through adoptFetchedSource on the GUI thread
    void startAsyncFetch( uint64_t totalMatches, int progressPercent );
    void adoptFetchedSource( uint64_t generation, SourceData data );
    // API pull of the union projection (lines + match bits); runs on a pool
    // thread and only touches the session
    static std::optional<SourceData> fetchSourceFromApi( klogg::api::FileId fileId,
                                                         klogg::api::SearchId searchId );
};

#endif
