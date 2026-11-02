/*
 * Copyright (C) 2009, 2010, 2011, 2012, 2017 Nicolas Bonnefon and other contributors
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

#ifndef LOGFILTEREDDATA_H
#define LOGFILTEREDDATA_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "abstractlogdata.h"
#include "encoding.h"
#include "engine_options.h"
#include "hsregularexpression.h"
#include "linetypes.h"
#include "logfiltereddataworker.h"
#include "posttarget.h"
#include "synchronization.h"
#include "throttler.h"

class LogData;

// A list of matches found in a LogData, it stores all the matching lines,
// which can be accessed using the AbstractLogData interface, together with
// the original line number where they were found.
// Constructing such objet does not start the search.
// This object should be constructed by a LogData.
class LogFilteredData : public AbstractLogData {
public:
    // Constructor used by LogData
    LogFilteredData( const LogData* logData, PostTarget postTarget,
                     EngineErrorHandler errorHandler,
                     std::shared_ptr<EngineOptionsHolder> optionsHolder = {} );
    ~LogFilteredData() override;

    // Starts the async search, sending newDataAvailable() when new data found.
    // If a search is already in progress this function will block until
    // it is done, so the application should call interruptSearch() first.
    void runSearch( const RegularExpressionPattern& regExp, LineNumber startLine,
                    LineNumber endLine );
    // Shortcut for runSearch on all file
    void runSearch( const RegularExpressionPattern& regExp );

    // Extend a completed matches search to a newer end line, scanning only
    // [previous end, endLine). Returns false when the existing search cannot
    // be extended (different pattern, unfinished, or no new lines), in which
    // case the caller must run a fresh search.
    bool extendSearch( const RegularExpressionPattern& regExp, LineNumber endLine );

    // Run search into a specific output array while reusing the same search pipeline.
    enum class SearchOutputArray {
        Matches,
        Events,
    };
    void runSearchIntoArray( const RegularExpressionPattern& regExp, SearchOutputArray outputArray,
                             LineNumber startLine, LineNumber endLine );
    void runSearchIntoArray( const RegularExpressionPattern& regExp,
                             SearchOutputArray outputArray );

    // These callbacks are invoked on the owner thread whenever the search
    // steps forward or finishes. The returned id identifies the callback so
    // it can be removed with the matching remove function.
    size_t addMatchesSearchProgressedListener( SearchProgressListener listener );
    size_t addEventsSearchProgressedListener( SearchProgressListener listener );
    void removeMatchesSearchProgressedListener( size_t id );
    void removeEventsSearchProgressedListener( size_t id );

    // Interrupt the running search if one is in progress.
    // Nothing is done if no search is in progress.
    void interruptSearch();
    // Clear the search and the list of results.
    void clearSearch( bool dropCache = false );

    // Returns the line number in the original LogData where the element
    // 'index' was found.
    LineNumber getMatchingLineNumber( LineNumber index ) const;
    // Returns the line 'index' in filterd log data that matches
    // given original line number
    LineNumber getLineIndexNumber( LineNumber lineNumber ) const;

    // Returns the number of lines in the source log data
    LinesCount getNbTotalLines() const;
    // Returns the number of matches (independently of the visibility)
    LinesCount getNbMatches() const;
    // Returns the number of marks (independently of the visibility)
    LinesCount getNbMarks() const;
    // Returns the number of events (independently of the visibility)
    LinesCount getNbEvents() const;

    LineType lineTypeByIndex( LineNumber index ) const;
    LineType lineTypeByLine( LineNumber lineNumber ) const;

    // Marks interface (delegated to a Marks object)

    // Add a mark at the given line
    void addMark( LineNumber line );
    // Add a batch of marks: the marks bitmap is merged once and the
    // combination arrays are rebuilt once at the end, instead of once per
    // mark. Lines outside the file are rejected exactly like in addMark.
    void addMarks( const std::vector<LineNumber>& lines );
    // Remove a batch of marks, the symmetric twin of addMarks: one bulk maximum
    // decides whether the cached longest mark may have gone away and the
    // combination arrays are rebuilt once instead of once per line.
    void removeMarks( const std::vector<LineNumber>& lines );
    // Get the first mark after the line passed
    OptionalLineNumber getMarkAfter( LineNumber line ) const;
    // Get the first mark before the line passed
    OptionalLineNumber getMarkBefore( LineNumber line ) const;
    // Delete the mark present on the passed line
    void deleteMark( LineNumber line );
    // Toggle presence of the mark on the passed line.
    void toggleMark( LineNumber line );
    // Completely clear the marks list.
    void clearMarks();
    // Get all marked lines
    std::vector<LineNumber> getMarks() const;

    // Events interface

    // Add an event at the given line
    void addEvent( LineNumber line );
    // Add a batch of events: bulk merge of the events bitmap plus one batch
    // read for the max length, with the combination arrays rebuilt once at
    // the end instead of once per event
    void addEvents( const std::vector<LineNumber>& lines );
    // Delete the event present on the passed line
    void deleteEvent( LineNumber line );
    // Completely clear the events list.
    void clearEvents();
    // Get all event lines
    std::vector<LineNumber> getEvents() const;
    // Check if a line has an event
    bool isLineEvent( LineNumber line ) const;

    // Changes what the AbstractLogData returns via its getXLines/getNbLines
    // API.
    enum class VisibilityFlags : unsigned int {
        None = 0u,
        Matches = 1u << 0,
        Marks = 1u << 1,
        Events = 1u << 2,
    };
    class Visibility {
    public:
        constexpr Visibility() = default;
        constexpr Visibility( VisibilityFlags flags )
            : flags_( flags )
        {
        }

        constexpr bool testFlag( VisibilityFlags flag ) const
        {
            return ( static_cast<unsigned int>( flags_ ) & static_cast<unsigned int>( flag ) )
                   != 0;
        }

        constexpr Visibility operator|( VisibilityFlags flags ) const
        {
            return Visibility( static_cast<VisibilityFlags>(
                static_cast<unsigned int>( flags_ ) | static_cast<unsigned int>( flags ) ) );
        }

        constexpr Visibility& operator|=( VisibilityFlags flags )
        {
            flags_ = static_cast<VisibilityFlags>( static_cast<unsigned int>( flags_ )
                                                   | static_cast<unsigned int>( flags ) );
            return *this;
        }

        constexpr bool operator==( const Visibility& other ) const = default;

    private:
        VisibilityFlags flags_ = VisibilityFlags::None;
    };

    void setVisibility( Visibility visibility );
    Visibility visibility() const;

    void iterateOverLines( const std::function<void( LineNumber )>& callback ) const;

    // RAII read token holding the shared lock on the result arrays: a batch of
    // queries (frame-sized loops in the API read channel) sees one consistent
    // snapshot with a single lock acquisition instead of one per line.
    class ReadToken {
    public:
        LineNumber getMatchingLineNumber( LineNumber index ) const;
        LineNumber getLineIndexNumber( LineNumber line ) const;
        LineType lineTypeByLine( LineNumber line ) const;
        LinesCount getNbLine() const;
        // Pure-match count (the union projection may be larger)
        LinesCount getNbMatches() const;

    private:
        friend class LogFilteredData;
        explicit ReadToken( const LogFilteredData* data );

        SharedLock lock_;
        const LogFilteredData* data_;
    };

    ReadToken readToken() const;

private:
    void handleSearchProgressed( LinesCount nbMatches, int progress, LineNumber initialLine );
    void handleSearchProgressedThrottled();

private:
    // Implementation of virtual functions
    std::string doGetLineString( LineNumber line ) const override;
    std::string doGetExpandedLineString( LineNumber line ) const override;
    klogg::vector<std::string> doGetLines( LineNumber first, LinesCount number ) const override;
    klogg::vector<std::string> doGetExpandedLines( LineNumber first,
                                                   LinesCount number ) const override;
    LineNumber doGetLineNumber( LineNumber index ) const override;
    LinesCount doGetNbLine() const override;
    LineLength doGetMaxLength() const override;
    LineLength doGetLineLength( LineNumber line ) const override;

    bool doSetDisplayEncoding( const char* encoding ) override;
    std::optional<klogg::Encoding> doGetDisplayEncoding() const override;

    void doAttachReader() const override;
    void doDetachReader() const override;

    // Insert new mark into filteredItemsCache_.
    void updateCacheWithMark( uint32_t index, LineNumber line );

    // Returns whether the line number passed is in our list of matching ones.
    // Caller must hold dataMutex_ (shared or unique).
    bool isLineMatched( LineNumber lineNumber ) const;
    // Returns wheither the passed line has a mark on it.
    // Caller must hold dataMutex_ (shared or unique).
    bool isLineMarked( LineNumber line ) const;
    // Caller must hold dataMutex_ (shared or unique).
    bool isLineEventUnlocked( LineNumber line ) const;
    // Caller must hold dataMutex_ (shared or unique).
    LineType lineTypeByLineUnlocked( LineNumber lineNumber ) const;

    // Guards the result array family below (the seven roaring bitmaps, the
    // three max lengths and visibility_): writers (search progress commits,
    // marks/events mirroring, visibility flips - all on the session executor)
    // take a unique lock, readers on arbitrary API consumer threads take a
    // shared one. Never call listeners or source LogData reads that can
    // re-enter this object while holding it unique.
    mutable SharedMutex dataMutex_;

    // List of the matching line numbers
    SearchResultArray matching_lines_;
    SearchResultArray marks_;
    SearchResultArray marks_and_matches_;
    SearchResultArray events_;
    SearchResultArray events_and_matches_;
    SearchResultArray events_and_marks_;
    SearchResultArray events_and_marks_and_matches_;

    SearchOutputArray activeSearchOutputArray_ = SearchOutputArray::Matches;

    const LogData* sourceLogData_;
    PostTarget postTarget_;
    std::shared_ptr<EngineOptionsHolder> optionsHolder_;

    LineLength maxLength_;
    LineLength maxLengthMarks_;
    LineLength maxLengthEvents_;

    Visibility visibility_;

    LogFilteredDataWorker workerThread_;
    std::shared_ptr<std::atomic<uint64_t>> workerGeneration_;
    std::atomic<bool> closing_{ false };

    Mutex searchProgressMutex_;
    std::tuple<LinesCount, int, LineNumber> searchProgress_;

    std::unique_ptr<klogg::TrailingThrottler> searchProgressThrottler_;

    std::unordered_map<size_t, SearchProgressListener> matchesSearchProgressedListeners_;
    std::unordered_map<size_t, SearchProgressListener> eventsSearchProgressedListeners_;
    size_t nextSearchProgressListenerId_ = 0;

    void dispatchSearchProgress( LinesCount nbMatches, int progress, LineNumber initialLine );

private:
    struct CachedSearchResult {
        SearchResultArray matching_lines;
        LineLength maxLength;
    };

    using SearchCacheKey = std::tuple<RegularExpressionPattern, LineNumber::UnderlyingType,
                                      LineNumber::UnderlyingType>;
    struct SearchCacheKeyHash {
        template <class T>
        void hash_combine( std::size_t& seed, const T& v ) const
        {
            seed ^= std::hash<T>()( v ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
        }
        std::size_t operator()( const SearchCacheKey& k ) const
        {
            size_t seed = std::hash<std::string>{}( std::get<0>( k ).pattern );

            hash_combine( seed, std::get<0>( k ).isPlainText );
            hash_combine( seed, std::get<0>( k ).isBoolean );
            hash_combine( seed, std::get<0>( k ).isCaseSensitive );
            hash_combine( seed, std::get<0>( k ).isExclude );
            hash_combine( seed, std::get<1>( k ) );
            hash_combine( seed, std::get<2>( k ) );
            return seed;
        }
    };

    std::unordered_map<SearchCacheKey, CachedSearchResult, SearchCacheKeyHash> searchResultsCache_;
    struct SearchRuntimeState {
        SearchCacheKey key{};
        LinesCount processed = 0_lcount;
    };

    SearchRuntimeState matchesSearchState_;
    SearchRuntimeState eventsSearchState_;
    // Matches already present before an extendSearch: worker progress reports
    // only the newly scanned range, so the public count adds this base back.
    LinesCount searchBaseMatches_ = 0_lcount;

    SearchCacheKey makeCacheKey( const RegularExpressionPattern& regExp, LineNumber startLine,
                                 LineNumber endLine )
    {
        return std::make_tuple( regExp, startLine.get(), endLine.get() );
    }

    void updateSearchResultsCache( const SearchCacheKey& searchKey );

    inline LineNumber getExpectedSearchEnd( const SearchCacheKey& cacheKey ) const
    {
        return LineNumber( std::get<2>( cacheKey ) );
    }

    // Utility functions; the caller must hold dataMutex_ (shared or unique)
    const SearchResultArray& currentResultArray() const;
    LineNumber findLogDataLine( LineNumber lineNum ) const;
    LineNumber findFilteredLine( LineNumber lineNum ) const;
    // Caller must hold dataMutex_ unique
    void clearSearchUnlocked();

    // update maxLengthMarks_ when a Marks was changed; caller holds dataMutex_
    // unique
    void updateMaxLengthMarks( OptionalLineNumber added_line, OptionalLineNumber removed_line );
    // Recompute maxLengthMarks_ from the marks bitmap; caller holds dataMutex_
    // unique
    void recomputeMaxLengthMarks();
    // update maxLengthEvents_ when an Event was changed; caller holds
    // dataMutex_ unique
    void updateMaxLengthEvents( OptionalLineNumber added_line, OptionalLineNumber removed_line );

    // Rebuild the combination arrays derived from the marks or events bitmap;
    // caller holds dataMutex_ unique
    void rebuildMarkCombinationArrays();
    void rebuildEventCombinationArrays();

    // Longest source line among the passed lines, with the line lengths read
    // as one batch instead of one file round per line. Lines outside the file
    // count as zero length, same as LogData::getLineLength. The result is
    // independent of the result arrays, so this runs without dataMutex_.
    LineLength maxSourceLineLength( const std::vector<LineNumber>& lines ) const;
};

constexpr LogFilteredData::Visibility operator|( LogFilteredData::VisibilityFlags a,
                                                 LogFilteredData::VisibilityFlags b )
{
    return LogFilteredData::Visibility( static_cast<LogFilteredData::VisibilityFlags>(
        static_cast<unsigned int>( a ) | static_cast<unsigned int>( b ) ) );
}

#endif
