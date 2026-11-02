/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
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

#ifndef LOGFILTEREDDATAWORKERTHREAD_H
#define LOGFILTEREDDATAWORKERTHREAD_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include <roaring.hh>
#include <roaring64map.hh>
#include <tbb/task_group.h>

#include "atomicflag.h"
#include "linetypes.h"
#include "logdataworker.h"
#include "regularexpression.h"
#include "synchronization.h"

class LogData;

// Class encapsulating a single matching line
// Contains the line number the line was found in and its content.
class MatchingLine {
public:
    MatchingLine( LineNumber line )
        : lineNumber_{ line }
    {
    }

    // Accessors
    LineNumber lineNumber() const
    {
        return lineNumber_;
    }

    bool operator<( const MatchingLine& other ) const
    {
        return lineNumber_ < other.lineNumber_;
    }

private:
    LineNumber lineNumber_;
};

// This is an array of matching lines.
// It shall be implemented for random lookup speed, so
// a fixed "in-place" array (vector) is probably fine.
using SearchResultArray = roaring::Roaring64Map;

struct SearchResults {
    SearchResultArray newMatches;
    LineLength maxLength;
    LinesCount processedLines;
};

// Called on the worker pool thread whenever a search steps forward.
using SearchProgressListener = std::function<void( LinesCount, int, LineNumber )>;

// This class is a mutex protected set of search result data.
// It is thread safe.
class SearchData {
public:
    // will clear new matches
    SearchResults takeCurrentResults() const;

    // Atomically add to all the existing search data.
    void addAll( LineLength length, const SearchResultArray& matches, LinesCount nbMatches,
                 LinesCount nbLinesProcessed );
    // Get the number of matches
    LinesCount getNbMatches() const;
    // Get the last matched line number
    // That is "last" as in biggest, not latest
    // 0 if no matches have been found yet
    LineNumber getLastMatchedLineNumber() const;

    LineNumber getLastProcessedLine() const;

    // Delete the match for the passed line (if it exist)
    void deleteMatch( LineNumber line );

    // Atomically clear the data.
    void clear();

private:
    mutable SharedMutex dataMutex_;

    SearchResultArray matches_;
    mutable SearchResultArray newMatches_;
    LineLength maxLength_{ 0 };
    LinesCount nbLinesProcessed_{ 0 };
    LinesCount nbMatches_{ 0 };
};

class SearchOperation {
public:
    SearchOperation( const LogData& sourceLogData, AtomicFlag& interruptRequested,
                     const RegularExpressionPattern& regExp, LineNumber startLine,
                     LineNumber endLine, std::shared_ptr<EngineOptionsHolder> optionsHolder = {} );
    virtual ~SearchOperation() = default;

    // Run the search operation, returns true if it has been done
    // and false if it has been cancelled (results not copied)
    virtual void run( SearchData& result ) = 0;

    void setProgressListener( SearchProgressListener listener )
    {
        progressListener_ = std::move( listener );
    }

    void setErrorHandler( EngineErrorHandler errorHandler )
    {
        errorHandler_ = std::move( errorHandler );
    }

protected:
    // Implement the common part of the search, passing
    // the shared results and the line to begin the search from.
    void doSearch( SearchData& result, LineNumber initialLine );

    AtomicFlag& interruptRequested_;
    const RegularExpressionPattern regexp_;
    const LogData& sourceLogData_;
    LineNumber startLine_;
    LineNumber endLine_;
    std::shared_ptr<EngineOptionsHolder> optionsHolder_;

    SearchProgressListener progressListener_ = []( LinesCount, int, LineNumber ) {};
    EngineErrorHandler errorHandler_;
};

class FullSearchOperation : public SearchOperation {
public:
    FullSearchOperation( const LogData& sourceLogData, AtomicFlag& interruptRequested,
                         const RegularExpressionPattern& regExp, LineNumber startLine,
                         LineNumber endLine, std::shared_ptr<EngineOptionsHolder> optionsHolder = {} )
        : SearchOperation( sourceLogData, interruptRequested, regExp, startLine, endLine,
                           std::move( optionsHolder ) )
    {
    }

    void run( SearchData& result ) override;
};

class LogFilteredDataWorker {
public:
    LogFilteredDataWorker( const LogData& sourceLogData, EngineErrorHandler errorHandler,
                           std::shared_ptr<EngineOptionsHolder> optionsHolder = {} );
    ~LogFilteredDataWorker() noexcept;

    LogFilteredDataWorker( const LogFilteredDataWorker& ) = delete;
    LogFilteredDataWorker& operator=( const LogFilteredDataWorker&& ) = delete;

    LogFilteredDataWorker( LogFilteredDataWorker&& ) = delete;
    LogFilteredDataWorker& operator=( LogFilteredDataWorker&& ) = delete;

    // Start the search with the passed regexp
    void search( const RegularExpressionPattern& regExp, LineNumber startLine, LineNumber endLine );

    // Interrupts the search if one is in progress
    void interrupt();

    // Interrupts and waits for the search to finish, cleaning up posted events.
    void waitForCompletion();

    // Set the callback invoked on the worker pool thread for each progress step.
    void setProgressCallback( SearchProgressListener progressCallback );

    // Generation counter, bumped by waitForCompletion so progress notifications
    // belonging to a cancelled search can be discarded.
    const std::shared_ptr<std::atomic<uint64_t>>& generation() const;

    // get the current indexing data
    SearchResults getSearchResults() const;

private:
    const LogData& sourceLogData_;
    AtomicFlag interruptRequested_;
    std::shared_ptr<EngineOptionsHolder> optionsHolder_;

    std::shared_ptr<std::atomic<uint64_t>> generation_ = std::make_shared<std::atomic<uint64_t>>( 0 );
    SearchProgressListener progressCallback_;

    tbb::task_group operationsPool_;
    Mutex operationsMutex_;

    // Shared indexing data
    SearchData searchData_;
    EngineErrorHandler errorHandler_;
};

#endif
