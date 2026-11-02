/*
 * Copyright (C) 2009, 2010, 2011, 2012, 2013, 2017 Nicolas Bonnefon and other contributors
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

// This file implements LogFilteredData
// It stores a pointer to the LogData that created it,
// so should always be destroyed before the LogData.

#include "log.h"

#include <chrono>
#include <algorithm>
#include <cassert>
#include <algorithm>
#include <functional>
#include <algorithm>
#include <numeric>
#include <algorithm>
#include <tuple>
#include <algorithm>
#include <vector>
#include <algorithm>

#include "logdata.h"
#include "logfiltereddata.h"

#include "readablesize.h"
#include "synchronization.h"

// Usual constructor: just copy the data, the search is started by runSearch()
LogFilteredData::LogFilteredData( const LogData* logData, PostTarget postTarget,
                                  EngineErrorHandler errorHandler,
                                  std::shared_ptr<EngineOptionsHolder> optionsHolder )
    : AbstractLogData()
    , matching_lines_( SearchResultArray() )
    , postTarget_( std::move( postTarget ) )
    , optionsHolder_( std::move( optionsHolder ) )
    , visibility_()
    , workerThread_( *logData, std::move( errorHandler ), optionsHolder_ )
{
    // Starts with an empty result list
    maxLength_ = 0_length;
    maxLengthMarks_ = 0_length;
    maxLengthEvents_ = 0_length;
    matchesSearchState_.processed = 0_lcount;
    eventsSearchState_.processed = 0_lcount;

    sourceLogData_ = logData;

    visibility_ = VisibilityFlags::Marks | VisibilityFlags::Matches;

    workerGeneration_ = workerThread_.generation();

    // Progress arrives on the worker pool thread; move it back to the owner
    // thread, dropping notifications belonging to an earlier generation.
    workerThread_.setProgressCallback(
        [ this, genPtr = workerGeneration_ ]( LinesCount nbMatches, int progress,
                                              LineNumber initialLine ) {
            const auto generation = genPtr->load();
            if ( closing_.load() ) {
                return;
            }
            postTarget_( [ this, genPtr, generation, nbMatches, progress, initialLine ] {
                if ( genPtr->load() != generation ) {
                    return;
                }
                handleSearchProgressed( nbMatches, progress, initialLine );
            } );
        } );

    searchProgressThrottler_ = std::make_unique<klogg::TrailingThrottler>(
        std::chrono::milliseconds( 100 ), [ this, genPtr = workerGeneration_ ] {
            const auto generation = genPtr->load();
            if ( closing_.load() ) {
                return;
            }
            postTarget_( [ this, genPtr, generation ] {
                if ( genPtr->load() != generation ) {
                    return;
                }
                handleSearchProgressedThrottled();
            } );
        } );
}

LogFilteredData::~LogFilteredData()
{
    // closing_ is set before waitForCompletion() bumps the generation, and
    // both callbacks read the generation before checking closing_, so any
    // flush or progress callback that observes a bumped generation also
    // observes closing_ and drops its post instead of touching this object.
    closing_.store( true );
    workerThread_.waitForCompletion();
    searchProgressThrottler_.reset();
}

void LogFilteredData::runSearch( const RegularExpressionPattern& regExp )
{
    runSearchIntoArray( regExp, SearchOutputArray::Matches, 0_lnum,
                        LineNumber( getNbTotalLines().get() ) );
}

// Run the search and send newDataAvailable() signals.
void LogFilteredData::runSearch( const RegularExpressionPattern& regExp, LineNumber startLine,
                                 LineNumber endLine )
{
    runSearchIntoArray( regExp, SearchOutputArray::Matches, startLine, endLine );
}

void LogFilteredData::runSearchIntoArray( const RegularExpressionPattern& regExp,
                                          SearchOutputArray outputArray )
{
    runSearchIntoArray( regExp, outputArray, 0_lnum, LineNumber( getNbTotalLines().get() ) );
}

void LogFilteredData::runSearchIntoArray( const RegularExpressionPattern& regExp,
                                          SearchOutputArray outputArray, LineNumber startLine,
                                          LineNumber endLine )
{
    LOG_DEBUG << "Entering runSearch";

    const auto options = optionsHolder_ ? optionsHolder_->get() : EngineOptions{};
    activeSearchOutputArray_ = outputArray;

    auto& searchState
        = ( outputArray == SearchOutputArray::Matches ) ? matchesSearchState_ : eventsSearchState_;

    bool shouldRunSearch = true;
    LinesCount cachedResultCount = 0_lcount;
    {
        UniqueLock lock( dataMutex_ );

        if ( outputArray == SearchOutputArray::Matches ) {
            clearSearchUnlocked();
        }
        else {
            interruptSearch();
            // Event searches rebuild Events independently from Matches.
            events_ = {};
            maxLengthEvents_ = 0_length;
            events_and_matches_ = matching_lines_;
            events_and_marks_ = marks_;
            events_and_marks_and_matches_ = marks_and_matches_;
        }

        searchState.key = makeCacheKey( regExp, startLine, endLine );
        searchState.processed = 0_lcount;
        LOG_INFO << "Search cache key: " << regExp.pattern << "_" << startLine.get() << "_"
                 << endLine.get();

        if ( outputArray == SearchOutputArray::Matches && options.useSearchResultsCache ) {
            const auto cachedResults = searchResultsCache_.find( searchState.key );
            if ( cachedResults != std::end( searchResultsCache_ ) ) {
                LOG_INFO << "Got result from cache";
                shouldRunSearch = false;
                matching_lines_ = cachedResults->second.matching_lines;
                maxLength_ = cachedResults->second.maxLength;
                marks_and_matches_ = matching_lines_ | marks_;
                events_and_matches_ = matching_lines_ | events_;
                events_and_marks_and_matches_ = marks_and_matches_ | events_;
                cachedResultCount = LinesCount( matching_lines_.cardinality() );
            }
        }
    }

    if ( !shouldRunSearch ) {
        // Listeners are invoked outside the lock: they may re-enter this
        // object through the API read channel
        dispatchSearchProgress( cachedResultCount, 100, startLine );
        return;
    }

    attachReader();
    workerThread_.search( regExp, startLine, endLine );
}

bool LogFilteredData::extendSearch( const RegularExpressionPattern& regExp, LineNumber endLine )
{
    UniqueLock lock( dataMutex_ );

    const auto& key = matchesSearchState_.key;
    const auto startLine = LineNumber( std::get<1>( key ) );
    const auto previousEnd = LineNumber( std::get<2>( key ) );
    const auto processed = matchesSearchState_.processed.get();

    if ( std::get<0>( key ) != regExp || previousEnd >= endLine || previousEnd < startLine
         || processed < previousEnd.get() ) {
        return false;
    }

    // Keep the already found matches; the worker starts at the previous end
    // and its progress reports only the new range, so remember the base.
    searchBaseMatches_ = LinesCount( matching_lines_.cardinality() );
    matchesSearchState_.key = makeCacheKey( regExp, startLine, endLine );

    attachReader();
    workerThread_.search( regExp, previousEnd, endLine );
    return true;
}

void LogFilteredData::interruptSearch()
{
    LOG_DEBUG << "Entering interruptSearch";

    workerThread_.interrupt();
}

void LogFilteredData::clearSearchUnlocked()
{
    interruptSearch();

    activeSearchOutputArray_ = SearchOutputArray::Matches;
    matching_lines_ = {};
    marks_and_matches_ = marks_;

    // Update events combination arrays
    events_and_matches_ = events_;
    events_and_marks_and_matches_ = events_and_marks_;

    maxLength_ = 0_length;
    matchesSearchState_.processed = 0_lcount;
    searchBaseMatches_ = 0_lcount;
}

void LogFilteredData::clearSearch( bool dropCache )
{
    {
        UniqueLock lock( dataMutex_ );
        clearSearchUnlocked();
    }

    if ( dropCache ) {
        // The cache is only consulted from the executor thread (runSearch),
        // so it stays outside the read-channel lock
        searchResultsCache_.clear();
    }
}

LineNumber LogFilteredData::getMatchingLineNumber( LineNumber matchNum ) const
{
    SharedLock lock( dataMutex_ );
    return findLogDataLine( matchNum );
}

LineNumber LogFilteredData::getLineIndexNumber( LineNumber lineNumber ) const
{
    SharedLock lock( dataMutex_ );
    return findFilteredLine( lineNumber );
}

// Scan the list for the 'lineNumber' passed
bool LogFilteredData::isLineMatched( LineNumber lineNumber ) const
{
    return matching_lines_.contains( lineNumber.get() );
}

LinesCount LogFilteredData::getNbTotalLines() const
{
    return sourceLogData_->getNbLine();
}

LinesCount LogFilteredData::getNbMatches() const
{
    SharedLock lock( dataMutex_ );
    return LinesCount( matching_lines_.cardinality() );
}

LinesCount LogFilteredData::getNbMarks() const
{
    SharedLock lock( dataMutex_ );
    return LinesCount( marks_.cardinality() );
}

LinesCount LogFilteredData::getNbEvents() const
{
    SharedLock lock( dataMutex_ );
    return LinesCount( events_.cardinality() );
}

LogFilteredData::LineType LogFilteredData::lineTypeByIndex( LineNumber index ) const
{
    SharedLock lock( dataMutex_ );
    return lineTypeByLineUnlocked( findLogDataLine( index ) );
}

LogFilteredData::LineType LogFilteredData::lineTypeByLine( LineNumber lineNumber ) const
{
    SharedLock lock( dataMutex_ );
    return lineTypeByLineUnlocked( lineNumber );
}

LogFilteredData::LineType LogFilteredData::lineTypeByLineUnlocked( LineNumber lineNumber ) const
{
    LineType line_type = LineTypeFlags::Plain;

    if ( isLineMarked( lineNumber ) )
        line_type |= LineTypeFlags::Mark;

    if ( isLineMatched( lineNumber ) )
        line_type |= LineTypeFlags::Match;

    if ( isLineEventUnlocked( lineNumber ) )
        line_type |= LineTypeFlags::Event;

    return line_type;
}

void LogFilteredData::iterateOverLines( const std::function<void( LineNumber )>& callback ) const
{
    using CallbackFn = std::function<void( LineNumber )>;
    SharedLock lock( dataMutex_ );
    const auto& currentResults = currentResultArray();
    currentResults.iterate(
        []( uint64_t line, void* context ) -> bool {
            auto* callbackFn = static_cast<CallbackFn*>( context );
            callbackFn->operator()( LineNumber( line ) );
            return true;
        },
        static_cast<void*>( const_cast<CallbackFn*>( &callback ) ) );
}

// Delegation to our Marks object

void LogFilteredData::toggleMark( LineNumber line )
{
    if ( ( line >= 0_lnum ) && line < sourceLogData_->getNbLine() ) {
        UniqueLock lock( dataMutex_ );
        if ( !marks_.addChecked( line.get() ) ) {
            marks_.remove( line.get() );
            updateMaxLengthMarks( {}, line );
        }
        else {
            updateMaxLengthMarks( line, {} );
        }
    }
    else {
        LOG_ERROR << "LogFilteredData::toggleMark trying to toggle a mark outside of the file.";
    }
}

void LogFilteredData::addMark( LineNumber line )
{
    addMarks( { line } );
}

void LogFilteredData::addMarks( const std::vector<LineNumber>& lines )
{
    // The max length is a monotone over-approximation of the marks set (adds
    // only raise it, removals recompute it), so one bulk maximum over the
    // batch is what the per-line max(maxLength, length) chain would produce
    const auto addedMaxLength = maxSourceLineLength( lines );

    UniqueLock lock( dataMutex_ );
    for ( const auto line : lines ) {
        if ( ( line >= 0_lnum ) && line < sourceLogData_->getNbLine() ) {
            marks_.add( line.get() );
        }
        else {
            LOG_ERROR << "LogFilteredData::addMarks trying to create a mark outside of the file.";
        }
    }

    maxLengthMarks_ = std::max( maxLengthMarks_, addedMaxLength );
    rebuildMarkCombinationArrays();
}

void LogFilteredData::removeMarks( const std::vector<LineNumber>& lines )
{
    // Mirror of addMarks: an out-of-file line cannot be marked, so removing it
    // is a no-op instead of the error addMarks raises. The cached longest mark
    // can only shrink when one of the removed lines was that long, so the
    // bitmap walk happens only when the bulk maximum says it has to.
    const auto removedMaxLength = maxSourceLineLength( lines );

    UniqueLock lock( dataMutex_ );
    for ( const auto line : lines ) {
        marks_.remove( line.get() );
    }

    if ( removedMaxLength >= maxLengthMarks_ ) {
        recomputeMaxLengthMarks();
    }
    rebuildMarkCombinationArrays();
}

bool LogFilteredData::isLineMarked( LineNumber line ) const
{
    return marks_.contains( line.get() );
}

OptionalLineNumber LogFilteredData::getMarkAfter( LineNumber line ) const
{
    SharedLock lock( dataMutex_ );
    OptionalLineNumber marked_line;
    const LineNumber::UnderlyingType rank = marks_.rank( line.get() );
    LineNumber::UnderlyingType nextMark;
    if ( marks_.select( rank, &nextMark ) ) {
        marked_line = LineNumber( nextMark );
    }

    return marked_line;
}

OptionalLineNumber LogFilteredData::getMarkBefore( LineNumber line ) const
{
    SharedLock lock( dataMutex_ );
    OptionalLineNumber marked_line;

    const LineNumber::UnderlyingType rank = marks_.rank( line.get() );

    if ( rank < 2 ) {
        return marked_line;
    }

    LineNumber::UnderlyingType nextMark;
    if ( marks_.select( rank - 2, &nextMark ) ) {
        marked_line = LineNumber( nextMark );
    }

    return marked_line;
}

void LogFilteredData::deleteMark( LineNumber line )
{
    UniqueLock lock( dataMutex_ );
    marks_.remove( line.get() );
    updateMaxLengthMarks( {}, line );
}

void LogFilteredData::updateMaxLengthMarks( OptionalLineNumber added_line,
                                            OptionalLineNumber removed_line )
{
    rebuildMarkCombinationArrays();

    if ( added_line.has_value() ) {
        maxLengthMarks_ = std::max( maxLengthMarks_, sourceLogData_->getLineLength( *added_line ) );
    }

    // Now update the max length if needed
    if ( removed_line.has_value()
         && sourceLogData_->getLineLength( *removed_line ) >= maxLengthMarks_ ) {
        LOG_DEBUG << "deleteMark recalculating longest mark";
        recomputeMaxLengthMarks();
    }
}

void LogFilteredData::recomputeMaxLengthMarks()
{
    maxLengthMarks_ = 0_length;
    marks_.iterate(
        []( uint64_t line, void* context ) -> bool {
            auto* self = static_cast<LogFilteredData*>( context );
            self->maxLengthMarks_
                = std::max( self->maxLengthMarks_,
                            self->sourceLogData_->getLineLength( LineNumber( line ) ) );
            return true;
        },
        static_cast<void*>( this ) );
}

void LogFilteredData::clearMarks()
{
    UniqueLock lock( dataMutex_ );
    marks_ = {};
    maxLengthMarks_ = 0_length;

    // Update combination arrays
    marks_and_matches_ = matching_lines_;
    events_and_marks_ = events_;
    events_and_marks_and_matches_ = events_and_matches_;
}

std::vector<LineNumber> LogFilteredData::getMarks() const
{
    SharedLock lock( dataMutex_ );
    std::vector<LineNumber> markedLines;
    marks_.iterate(
        []( uint64_t line, void* context ) -> bool {
            static_cast<std::vector<LineNumber>*>( context )->push_back( LineNumber( line ) );
            return true;
        },
        static_cast<void*>( &markedLines ) );

    return markedLines;
}

void LogFilteredData::updateMaxLengthEvents( OptionalLineNumber added_line,
                                             OptionalLineNumber removed_line )
{
    rebuildEventCombinationArrays();

    if ( added_line.has_value() ) {
        maxLengthEvents_ = std::max( maxLengthEvents_, sourceLogData_->getLineLength( *added_line ) );
    }

    // Now update the max length if needed
    if ( removed_line.has_value()
         && sourceLogData_->getLineLength( *removed_line ) >= maxLengthEvents_ ) {
        LOG_DEBUG << "deleteEvent recalculating longest event";
        maxLengthEvents_ = 0_length;
        events_.iterate(
            []( uint64_t line, void* context ) -> bool {
                auto* self = static_cast<LogFilteredData*>( context );
                self->maxLengthEvents_
                    = std::max( self->maxLengthEvents_,
                            self->sourceLogData_->getLineLength( LineNumber( line ) ) );
                return true;
            },
            static_cast<void*>( this ) );
    }
}

void LogFilteredData::clearEvents()
{
    UniqueLock lock( dataMutex_ );
    events_ = {};
    maxLengthEvents_ = 0_length;

    // Update combination arrays
    events_and_matches_ = matching_lines_;
    events_and_marks_ = marks_;
    events_and_marks_and_matches_ = marks_and_matches_;
}

std::vector<LineNumber> LogFilteredData::getEvents() const
{
    SharedLock lock( dataMutex_ );
    std::vector<LineNumber> eventLines;
    events_.iterate(
        []( uint64_t line, void* context ) -> bool {
            static_cast<std::vector<LineNumber>*>( context )->push_back( LineNumber( line ) );
            return true;
        },
        static_cast<void*>( &eventLines ) );

    return eventLines;
}

void LogFilteredData::addEvent( LineNumber line )
{
    UniqueLock lock( dataMutex_ );
    OptionalLineNumber removed_line;
    if ( events_.contains( line.get() ) ) {
        removed_line = line;
        events_.remove( line.get() );
    }

    events_.add( line.get() );

    updateMaxLengthEvents( line, removed_line );
}

void LogFilteredData::addEvents( const std::vector<LineNumber>& lines )
{
    // See addMarks: the bulk maximum is what the per-line chain of
    // max(maxLengthEvents_, length) updates would leave behind (the
    // recompute branch of updateMaxLengthEvents only fires when the added
    // line is already the longest one, and then it computes that same value)
    const auto addedMaxLength = maxSourceLineLength( lines );

    UniqueLock lock( dataMutex_ );
    for ( const auto line : lines ) {
        events_.add( line.get() );
    }

    maxLengthEvents_ = std::max( maxLengthEvents_, addedMaxLength );
    rebuildEventCombinationArrays();
}

void LogFilteredData::rebuildMarkCombinationArrays()
{
    marks_and_matches_ = matching_lines_ | marks_;
    events_and_marks_ = marks_ | events_;
    events_and_marks_and_matches_ = marks_and_matches_ | events_;
}

void LogFilteredData::rebuildEventCombinationArrays()
{
    events_and_matches_ = matching_lines_ | events_;
    events_and_marks_ = marks_ | events_;
    events_and_marks_and_matches_ = marks_and_matches_ | events_;
}

// One file session and decoder serve a whole block of lines; blocks bound the
// decoded text held at once while still amortizing the per-call overhead
LineLength LogFilteredData::maxSourceLineLength( const std::vector<LineNumber>& lines ) const
{
    constexpr size_t kLineLengthBlockSize = 4096;

    const auto nbLines = sourceLogData_->getNbLine();
    LineLength maxLength = 0_length;

    klogg::vector<LineNumber> block;
    block.reserve( std::min( lines.size(), kLineLengthBlockSize ) );
    const auto measureBlock = [ & ] {
        // getLineLength is the untabified length of the decoded line, which
        // is what the batch of plain decoded lines gives once untabified
        for ( auto& text : sourceLogData_->getLinesForLineNumbers( block ) ) {
            maxLength = std::max(
                maxLength, LineLength{ static_cast<LineLength::UnderlyingType>(
                                untabify( std::move( text ) ).size() ) } );
        }
        block.clear();
    };

    for ( const auto line : lines ) {
        // Out of range lines have zero length for getLineLength
        if ( ( line < 0_lnum ) || ( line >= nbLines ) ) {
            continue;
        }
        block.push_back( line );
        if ( block.size() >= kLineLengthBlockSize ) {
            measureBlock();
        }
    }

    if ( !block.empty() ) {
        measureBlock();
    }

    return maxLength;
}

void LogFilteredData::deleteEvent( LineNumber line )
{
    UniqueLock lock( dataMutex_ );
    OptionalLineNumber removed_line;
    if ( events_.contains( line.get() ) ) {
        removed_line = line;
        events_.remove( line.get() );
        updateMaxLengthEvents( {}, removed_line );
    }
}

bool LogFilteredData::isLineEvent( LineNumber line ) const
{
    SharedLock lock( dataMutex_ );
    return isLineEventUnlocked( line );
}

bool LogFilteredData::isLineEventUnlocked( LineNumber line ) const
{
    return events_.contains( line.get() );
}

void LogFilteredData::setVisibility( Visibility visi )
{
    UniqueLock lock( dataMutex_ );
    visibility_ = visi;
}

LogFilteredData::Visibility LogFilteredData::visibility() const
{
    SharedLock lock( dataMutex_ );
    return visibility_;
}

size_t LogFilteredData::addMatchesSearchProgressedListener( SearchProgressListener listener )
{
    const auto id = nextSearchProgressListenerId_++;
    matchesSearchProgressedListeners_.emplace( id, std::move( listener ) );
    return id;
}

size_t LogFilteredData::addEventsSearchProgressedListener( SearchProgressListener listener )
{
    const auto id = nextSearchProgressListenerId_++;
    eventsSearchProgressedListeners_.emplace( id, std::move( listener ) );
    return id;
}

void LogFilteredData::removeMatchesSearchProgressedListener( size_t id )
{
    matchesSearchProgressedListeners_.erase( id );
}

void LogFilteredData::removeEventsSearchProgressedListener( size_t id )
{
    eventsSearchProgressedListeners_.erase( id );
}

void LogFilteredData::dispatchSearchProgress( LinesCount nbMatches, int progress,
                                              LineNumber initialLine )
{
    const auto& listeners = ( activeSearchOutputArray_ == SearchOutputArray::Matches )
                                ? matchesSearchProgressedListeners_
                                : eventsSearchProgressedListeners_;
    for ( const auto& listenerEntry : listeners ) {
        listenerEntry.second( nbMatches, progress, initialLine );
    }
}

LogFilteredData::ReadToken::ReadToken( const LogFilteredData* data )
    : lock_( data->dataMutex_ )
    , data_( data )
{
}

LogFilteredData::ReadToken LogFilteredData::readToken() const
{
    return ReadToken( this );
}

LineNumber LogFilteredData::ReadToken::getMatchingLineNumber( LineNumber index ) const
{
    return data_->findLogDataLine( index );
}

LineNumber LogFilteredData::ReadToken::getLineIndexNumber( LineNumber line ) const
{
    return data_->findFilteredLine( line );
}

LogFilteredData::LineType LogFilteredData::ReadToken::lineTypeByLine( LineNumber line ) const
{
    return data_->lineTypeByLineUnlocked( line );
}

LinesCount LogFilteredData::ReadToken::getNbLine() const
{
    return LinesCount( data_->currentResultArray().cardinality() );
}

LinesCount LogFilteredData::ReadToken::getNbMatches() const
{
    return LinesCount( data_->matching_lines_.cardinality() );
}

void LogFilteredData::updateSearchResultsCache( const SearchCacheKey& searchKey )
{
    const auto options = optionsHolder_ ? optionsHolder_->get() : EngineOptions{};
    if ( !options.useSearchResultsCache ) {
        return;
    }

    if ( searchKey == SearchCacheKey{} ) {
        return;
    }

    const uint64_t maxCacheLines = options.searchResultsCacheLines;

    if ( matching_lines_.cardinality() > maxCacheLines ) {
        LOG_DEBUG << "LogFilteredData: too many matches to place in cache";
    }
    else {
        LOG_INFO << "LogFilteredData: caching results for key " << std::get<0>( searchKey ).pattern
                 << "_" << std::get<1>( searchKey ) << "_" << std::get<2>( searchKey );

        searchResultsCache_[ searchKey ] = { matching_lines_, maxLength_ };
        auto cacheSize = std::accumulate( searchResultsCache_.cbegin(), searchResultsCache_.cend(),
                                          uint64_t{ 0 }, []( const auto& acc, const auto& next ) {
                                              return acc + next.second.matching_lines.cardinality();
                                          } );

        LOG_INFO << "LogFilteredData: cache size " << cacheSize;

        auto cachedResult = std::begin( searchResultsCache_ );
        while ( cachedResult != std::end( searchResultsCache_ ) && cacheSize > maxCacheLines ) {

            if ( cachedResult->first == searchKey ) {
                ++cachedResult;
                continue;
            }

            cacheSize -= cachedResult->second.matching_lines.cardinality();
            cachedResult = searchResultsCache_.erase( cachedResult );
        }
    }
}

void LogFilteredData::handleSearchProgressed( LinesCount nbMatches, int progress,
                                              LineNumber initialLine )
{
    assert( nbMatches >= 0_lcount );

    const auto searchResults = workerThread_.getSearchResults();

    {
        UniqueLock lock( dataMutex_ );

        if ( activeSearchOutputArray_ == SearchOutputArray::Matches ) {
            matching_lines_ |= searchResults.newMatches;
            marks_and_matches_ |= searchResults.newMatches;
            maxLength_ = std::max( maxLength_, searchResults.maxLength );
        }
        else {
            events_ |= searchResults.newMatches;
            maxLengthEvents_ = searchResults.maxLength;
            events_and_marks_ = marks_ | events_;
        }

        // Update combination arrays in both modes
        events_and_matches_ = matching_lines_ | events_;
        events_and_marks_and_matches_ = marks_and_matches_ | events_;

        auto& searchState = ( activeSearchOutputArray_ == SearchOutputArray::Matches )
                                ? matchesSearchState_
                                : eventsSearchState_;
        searchState.processed = searchResults.processedLines;

        if ( activeSearchOutputArray_ == SearchOutputArray::Matches && progress == 100
             && searchState.processed.get() == getExpectedSearchEnd( searchState.key ).get() ) {
            updateSearchResultsCache( searchState.key );
        }

        if ( progress == 100 ) {
            LOG_INFO << "Matches size "
                     << readableSize( matching_lines_.getSizeInBytes( false ) ) << ", marks size "
                     << readableSize( marks_.getSizeInBytes( false ) ) << ", union size "
                     << readableSize( marks_and_matches_.getSizeInBytes( false ) );
        }
    }

    {
        ScopedLock lock( searchProgressMutex_ );
        const auto reportedMatches = ( activeSearchOutputArray_ == SearchOutputArray::Matches )
                                         ? nbMatches + searchBaseMatches_
                                         : nbMatches;
        searchProgress_ = std::make_tuple( reportedMatches, progress, initialLine );
    }

    searchProgressThrottler_->trigger();

    if ( progress == 100 ) {
        detachReader();
    }
}

void LogFilteredData::handleSearchProgressedThrottled()
{
    LinesCount nbMatches;
    int progress;
    LineNumber initialLine;
    {
        ScopedLock lock( searchProgressMutex_ );
        std::tie( nbMatches, progress, initialLine ) = searchProgress_;
    }
    dispatchSearchProgress( nbMatches, progress, initialLine );
}

LineNumber LogFilteredData::findLogDataLine( LineNumber index ) const
{
    const auto& currentResults = currentResultArray();

    LineNumber::UnderlyingType line = {};
    if ( currentResults.select( index.get(), &line ) ) {
        return LineNumber( line );
    }
    else {
        if ( !currentResults.isEmpty() ) {
            LOG_ERROR << "Index too big in LogFilteredData: " << index << " cache size "
                      << currentResults.cardinality();
        }
        return maxValue<LineNumber>();
    }
}

const SearchResultArray& LogFilteredData::currentResultArray() const
{
    // Check for events first as they can be combined with marks and matches
    if ( visibility_.testFlag( VisibilityFlags::Events ) ) {
        if ( visibility_.testFlag( VisibilityFlags::Marks )
             && visibility_.testFlag( VisibilityFlags::Matches ) ) {
            return events_and_marks_and_matches_;
        }
        else if ( visibility_.testFlag( VisibilityFlags::Marks ) ) {
            return events_and_marks_;
        }
        else if ( visibility_.testFlag( VisibilityFlags::Matches ) ) {
            return events_and_matches_;
        }
        else {
            return events_;
        }
    }

    if ( visibility_.testFlag( VisibilityFlags::Marks )
         && visibility_.testFlag( VisibilityFlags::Matches ) ) {
        return marks_and_matches_;
    }
    else if ( visibility_.testFlag( VisibilityFlags::Matches ) ) {
        return matching_lines_;
    }
    else {
        return marks_;
    }
}

LineNumber LogFilteredData::findFilteredLine( LineNumber lineNum ) const
{
    LineNumber::UnderlyingType index = currentResultArray().rank( lineNum.get() );

    if ( index > 0 ) {
        index--;
    }
    return LineNumber( index );
}

// Implementation of the virtual function.
std::string LogFilteredData::doGetLineString( LineNumber index ) const
{
    LineNumber line;
    {
        SharedLock lock( dataMutex_ );
        line = findLogDataLine( index );
    }
    return sourceLogData_->getLineString( line );
}

// Implementation of the virtual function.
std::string LogFilteredData::doGetExpandedLineString( LineNumber index ) const
{
    LineNumber line;
    {
        SharedLock lock( dataMutex_ );
        line = findLogDataLine( index );
    }
    return sourceLogData_->getExpandedLineString( line );
}

// Implementation of the virtual function.
klogg::vector<std::string>
LogFilteredData::doGetLines( LineNumber first_line, LinesCount number ) const
{
    // Map the projection indices to source lines up front so the source
    // fetches the whole set with one file session instead of one single-line
    // read round per line (B5d); the array snapshot is taken once and the
    // file reads run outside the lock
    klogg::vector<LineNumber> sourceLines;
    {
        SharedLock lock( dataMutex_ );
        sourceLines.reserve( number.get() );
        for ( auto index = first_line; index < first_line + number; ++index ) {
            sourceLines.push_back( findLogDataLine( index ) );
        }
    }
    return sourceLogData_->getLinesForLineNumbers( sourceLines );
}

// Implementation of the virtual function.
klogg::vector<std::string>
LogFilteredData::doGetExpandedLines( LineNumber first_line, LinesCount number ) const
{
    auto lines = doGetLines( first_line, number );
    for ( auto& line : lines ) {
        line = untabify( std::move( line ) );
    }
    return lines;
}

LineNumber LogFilteredData::doGetLineNumber( LineNumber index ) const
{
    return getMatchingLineNumber( index );
}

// Implementation of the virtual function.
LinesCount LogFilteredData::doGetNbLine() const
{
    SharedLock lock( dataMutex_ );
    const LinesCount::UnderlyingType nbLines = currentResultArray().cardinality();
    return LinesCount( nbLines );
}

// Implementation of the virtual function.
LineLength LogFilteredData::doGetMaxLength() const
{
    SharedLock lock( dataMutex_ );
    return std::max( std::max( maxLength_, maxLengthMarks_ ), maxLengthEvents_ );
}

// Implementation of the virtual function.
LineLength LogFilteredData::doGetLineLength( LineNumber lineNum ) const
{
    LineNumber line;
    {
        SharedLock lock( dataMutex_ );
        line = findLogDataLine( lineNum );
    }
    return sourceLogData_->getLineLength( line );
}

bool LogFilteredData::doSetDisplayEncoding( const char* encoding )
{
    LOG_DEBUG << "AbstractLogData::setDisplayEncoding: " << encoding;
    // The filtered view decodes through its source data: setting the encoding
    // on the source is what reindexes (and reports the change), never this
    return false;
}

std::optional<klogg::Encoding> LogFilteredData::doGetDisplayEncoding() const
{
    return sourceLogData_->getDisplayEncoding();
}

void LogFilteredData::doAttachReader() const
{
    sourceLogData_->attachReader();
}

void LogFilteredData::doDetachReader() const
{
    sourceLogData_->detachReader();
}
