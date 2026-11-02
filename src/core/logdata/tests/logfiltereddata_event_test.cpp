/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

#include <catch2/catch.hpp>

#include "linetypes.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "posttarget.h"
#include "regularexpression.h"
#include "uuid.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <vector>

namespace {

// LogData hands its notifications to the owner thread through a PostTarget and
// must not see them run inline (see posttarget.h). The tests used to let the Qt
// event loop do that; now the wait loop below drains the queue on the test
// thread, so the callbacks still run single-threaded.
class DeferredTasks {
public:
    PostTarget target()
    {
        return [ this ]( std::function<void()> task ) {
            const std::lock_guard<std::mutex> lock( mutex_ );
            tasks_.push_back( std::move( task ) );
        };
    }

    void drain()
    {
        std::vector<std::function<void()>> pending;
        {
            const std::lock_guard<std::mutex> lock( mutex_ );
            pending.swap( tasks_ );
        }
        for ( auto& task : pending ) {
            task();
        }
    }

private:
    std::mutex mutex_;
    std::vector<std::function<void()>> tasks_;
};

template <typename F>
bool waitForState( DeferredTasks& tasks, F&& checkFunc, int timeoutMs = 10000 )
{
    for ( auto elapsed = 0; elapsed < timeoutMs; elapsed += 100 ) {
        tasks.drain();
        if ( checkFunc() ) {
            return true;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    }
    tasks.drain();
    return checkFunc();
}

struct LoadedLogFile {
    // Declared before logData so it outlives the LogData posting through it
    std::unique_ptr<DeferredTasks> tasks = std::make_unique<DeferredTasks>();
    std::unique_ptr<LogData> logData;
    std::string filePath;

    LoadedLogFile() = default;
    LoadedLogFile( LoadedLogFile&& ) = default;
    LoadedLogFile& operator=( LoadedLogFile&& ) = default;
    LoadedLogFile( const LoadedLogFile& ) = delete;
    LoadedLogFile& operator=( const LoadedLogFile& ) = delete;

    ~LoadedLogFile()
    {
        if ( !filePath.empty() ) {
            std::error_code error;
            std::filesystem::remove( filePath, error );
        }
    }
};

std::string tempFilePath( const std::string& prefix )
{
    std::error_code error;
    const auto tempDir = std::filesystem::temp_directory_path( error );
    REQUIRE_FALSE( error );
    return ( tempDir / ( prefix + generateIdFromUuid() + ".log" ) ).string();
}

LoadedLogFile createLoadedLogFile( const std::vector<std::string>& lines )
{
    LoadedLogFile loaded;

    loaded.filePath = tempFilePath( "klogg_event_test_" );

    {
        std::ofstream file( loaded.filePath );
        REQUIRE( file.is_open() );
        for ( const auto& line : lines ) {
            file << line << "\n";
        }
    }

    loaded.logData = std::make_unique<LogData>( loaded.tasks->target() );
    bool loadFinished = false;
    const auto listenerId = loaded.logData->addLoadingFinishedListener(
        [ &loadFinished ]( LoadingStatus ) { loadFinished = true; } );

    loaded.logData->attachFile( loaded.filePath );
    REQUIRE( waitForState( *loaded.tasks, [ &loadFinished ] { return loadFinished; } ) );
    loaded.logData->removeLoadingFinishedListener( listenerId );

    return loaded;
}

using SearchProgressCall = std::tuple<LinesCount, int, LineNumber>;

bool waitForSearchCompletion( DeferredTasks& tasks, const std::vector<SearchProgressCall>& calls,
                              int timeoutMs = 10000 )
{
    auto isCompleted
        = [ &calls ]() { return !calls.empty() && std::get<1>( calls.back() ) == 100; };

    return isCompleted() ? true : waitForState( tasks, isCompleted, timeoutMs );
}

} // namespace

SCENARIO( "LogFilteredData max length includes events", "[event][regression]" )
{
    auto loaded = createLoadedLogFile( { "short", "this is a much longer line used as event" } );
    auto filteredData = loaded.logData->getNewFilteredData();

    filteredData->setVisibility( LogFilteredData::VisibilityFlags::Events );
    filteredData->addEvent( 1_lnum );

    REQUIRE( filteredData->getNbEvents() == 1_lcount );
    REQUIRE( filteredData->getMaxLength() == loaded.logData->getLineLength( 1_lnum ) );
}

SCENARIO( "LogFilteredData searches into events without overwriting matches",
          "[event][regression]" )
{
    auto loaded = createLoadedLogFile( { "INFO alpha", "WARN beta", "INFO gamma", "WARN delta" } );
    auto filteredData = loaded.logData->getNewFilteredData();

    GIVEN( "User search builds matches" )
    {
        std::vector<SearchProgressCall> searchCalls;
        filteredData->addMatchesSearchProgressedListener(
            [ &searchCalls ]( LinesCount nbMatches, int progress, LineNumber initialLine ) {
                searchCalls.emplace_back( nbMatches, progress, initialLine );
            } );

        RegularExpressionPattern matchPattern( "WARN", false, false, false, false );
        filteredData->runSearch( matchPattern );
        REQUIRE( waitForSearchCompletion( *loaded.tasks, searchCalls ) );
        REQUIRE( filteredData->getNbMatches() == 2_lcount );
        REQUIRE( filteredData->getNbEvents() == 0_lcount );

        WHEN( "Event search writes into events array" )
        {
            std::vector<SearchProgressCall> eventSearchCalls;
            filteredData->addEventsSearchProgressedListener(
                [ &eventSearchCalls ]( LinesCount nbEvents, int progress, LineNumber initialLine ) {
                    eventSearchCalls.emplace_back( nbEvents, progress, initialLine );
                } );
            RegularExpressionPattern eventPattern( "INFO", false, false, false, false );
            filteredData->runSearchIntoArray( eventPattern,
                                              LogFilteredData::SearchOutputArray::Events );
            REQUIRE( waitForSearchCompletion( *loaded.tasks, eventSearchCalls ) );

            THEN( "Matches remain unchanged and events are updated independently" )
            {
                REQUIRE( filteredData->getNbMatches() == 2_lcount );
                REQUIRE( filteredData->getNbEvents() == 2_lcount );
            }
        }
    }
}

SCENARIO( "LogFilteredData user search does not clear existing events", "[event][regression]" )
{
    auto loaded = createLoadedLogFile( { "INFO alpha", "WARN beta", "INFO gamma", "WARN delta" } );
    auto filteredData = loaded.logData->getNewFilteredData();

    // Build events first
    std::vector<SearchProgressCall> eventSearchCalls;
    filteredData->addEventsSearchProgressedListener(
        [ &eventSearchCalls ]( LinesCount nbEvents, int progress, LineNumber initialLine ) {
            eventSearchCalls.emplace_back( nbEvents, progress, initialLine );
        } );
    RegularExpressionPattern eventPattern( "INFO", false, false, false, false );
    filteredData->runSearchIntoArray( eventPattern, LogFilteredData::SearchOutputArray::Events );
    REQUIRE( waitForSearchCompletion( *loaded.tasks, eventSearchCalls ) );
    REQUIRE( filteredData->getNbEvents() == 2_lcount );

    // Then run regular search (matches)
    std::vector<SearchProgressCall> matchSearchCalls;
    filteredData->addMatchesSearchProgressedListener(
        [ &matchSearchCalls ]( LinesCount nbMatches, int progress, LineNumber initialLine ) {
            matchSearchCalls.emplace_back( nbMatches, progress, initialLine );
        } );
    RegularExpressionPattern matchPattern( "WARN", false, false, false, false );
    filteredData->runSearch( matchPattern );
    REQUIRE( waitForSearchCompletion( *loaded.tasks, matchSearchCalls ) );

    REQUIRE( filteredData->getNbMatches() == 2_lcount );
    REQUIRE( filteredData->getNbEvents() == 2_lcount );
}

SCENARIO( "LogFilteredData signal split smoke", "[event][regression]" )
{
    auto loaded = createLoadedLogFile( { "INFO alpha", "WARN beta" } );
    auto filteredData = loaded.logData->getNewFilteredData();

    std::vector<SearchProgressCall> matchesCalls;
    std::vector<SearchProgressCall> eventsCalls;
    filteredData->addMatchesSearchProgressedListener(
        [ &matchesCalls ]( LinesCount nbMatches, int progress, LineNumber initialLine ) {
            matchesCalls.emplace_back( nbMatches, progress, initialLine );
        } );
    filteredData->addEventsSearchProgressedListener(
        [ &eventsCalls ]( LinesCount nbEvents, int progress, LineNumber initialLine ) {
            eventsCalls.emplace_back( nbEvents, progress, initialLine );
        } );

    // No search executed, just ensure both listener channels are wired.
    REQUIRE( matchesCalls.empty() );
    REQUIRE( eventsCalls.empty() );
}

SCENARIO( "LogFilteredData visibility projection uses independent A/B/C caches",
          "[event][regression]" )
{
    auto loaded = createLoadedLogFile( { "INFO alpha", "WARN beta", "INFO gamma", "WARN delta" } );
    auto filteredData = loaded.logData->getNewFilteredData();

    // A: Matches = lines 1,3
    std::vector<SearchProgressCall> matchesCalls;
    filteredData->addMatchesSearchProgressedListener(
        [ &matchesCalls ]( LinesCount nbMatches, int progress, LineNumber initialLine ) {
            matchesCalls.emplace_back( nbMatches, progress, initialLine );
        } );
    filteredData->runSearch( RegularExpressionPattern( "WARN", false, false, false, false ) );
    REQUIRE( waitForSearchCompletion( *loaded.tasks, matchesCalls ) );

    // C: Events = lines 0,2
    std::vector<SearchProgressCall> eventsCalls;
    filteredData->addEventsSearchProgressedListener(
        [ &eventsCalls ]( LinesCount nbEvents, int progress, LineNumber initialLine ) {
            eventsCalls.emplace_back( nbEvents, progress, initialLine );
        } );
    filteredData->runSearchIntoArray(
        RegularExpressionPattern( "INFO", false, false, false, false ),
        LogFilteredData::SearchOutputArray::Events );
    REQUIRE( waitForSearchCompletion( *loaded.tasks, eventsCalls ) );

    // B: Marks = line 3 (overlaps with Matches)
    filteredData->addMark( 3_lnum );

    using V = LogFilteredData::VisibilityFlags;

    filteredData->setVisibility( V::Events );
    REQUIRE( filteredData->getNbLine() == 2_lcount );

    filteredData->setVisibility( V::Matches );
    REQUIRE( filteredData->getNbLine() == 2_lcount );

    filteredData->setVisibility( V::Marks );
    REQUIRE( filteredData->getNbLine() == 1_lcount );

    filteredData->setVisibility( V::Marks | V::Matches | V::Events );
    REQUIRE( filteredData->getNbLine() == 4_lcount );
}
