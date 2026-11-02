/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

#include <catch2/catch.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTextCodec>
#include <QTextStream>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "klogg/api.h"

#include "logdata.h"

using namespace klogg::api;

namespace {

constexpr int kWaitTimeoutMs = 20000;

// A completion for a three line file has no reason to take seconds: the bound
// is only this generous so a loaded machine cannot make a test flaky, while a
// completion that never arrives still fails in seconds instead of waiting out
// the engine's ten minute indexing timeout
constexpr int kCompletionTimeoutMs = 8000;

struct TempLogFile {
    QString path;

    TempLogFile() = default;
    TempLogFile( const TempLogFile& ) = delete;
    TempLogFile& operator=( const TempLogFile& ) = delete;
    TempLogFile( TempLogFile&& ) = default;
    TempLogFile& operator=( TempLogFile&& ) = default;

    ~TempLogFile()
    {
        if ( !path.isEmpty() ) {
            QFile::remove( path );
        }
    }
};

QString tempFilePath()
{
    return QString( "%1/klogg_api_test_%2.log" )
        .arg( QDir::tempPath(), QUuid::createUuid().toString( QUuid::WithoutBraces ) );
}

TempLogFile writeTempLogFile( const QStringList& lines )
{
    TempLogFile file;
    file.path = tempFilePath();

    QFile out( file.path );
    REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Text ) );
    QTextStream stream( &out );
    for ( const auto& line : lines ) {
        stream << line << "\n";
    }
    out.close();
    return file;
}

TempLogFile writeTempUtf16LeFile( const QStringList& lines )
{
    TempLogFile file;
    file.path = tempFilePath();

    const auto* codec = QTextCodec::codecForName( "UTF-16LE" );
    REQUIRE( codec );

    QFile out( file.path );
    REQUIRE( out.open( QIODevice::WriteOnly ) );
    out.write( "\xFF\xFE", 2 );
    out.write( codec->fromUnicode( lines.join( QLatin1Char( '\n' ) ) + QLatin1Char( '\n' ) ) );
    out.close();
    return file;
}

// ~1.5M padded lines (~165MB): indexing takes ~1s, so an interrupt issued
// after the first progress report reliably lands mid-indexing
TempLogFile writeLargeTempLogFile()
{
    TempLogFile file;
    file.path = tempFilePath();

    QFile out( file.path );
    REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Text ) );
    QTextStream stream( &out );
    const QString padding( 100, QLatin1Char( 'x' ) );
    for ( auto i = 0; i < 1500000; ++i ) {
        stream << "match line " << i << " " << padding << "\n";
    }
    out.close();
    return file;
}

void appendLines( const QString& path, const QStringList& lines )
{
    QFile out( path );
    REQUIRE( out.open( QIODevice::Append | QIODevice::Text ) );
    QTextStream stream( &out );
    for ( const auto& line : lines ) {
        stream << line << "\n";
    }
}

std::unique_ptr<Session> createSession( const EngineOptions& options = {} )
{
    auto session = Session::create( options );
    REQUIRE( session.has_value() );
    return std::move( session.value() );
}

FileInfo openSyncOrFail( Session& session, const QString& path )
{
    auto info = session.openFileSync( path.toStdString() );
    REQUIRE( info.has_value() );
    return *info;
}

bool waitForSearchDone( Session& session, SearchId id, int timeoutMs = kWaitTimeoutMs )
{
    QElapsedTimer timer;
    timer.start();
    while ( timer.elapsed() < timeoutMs ) {
        const auto progress = session.searchProgress( id );
        if ( !progress ) {
            return false;
        }
        if ( progress->progress >= 1.0f ) {
            return true;
        }
        QThread::msleep( 10 );
    }
    return false;
}

// Collects events from a worker thread; handlers fire on the session worker
struct EventCollector {
    std::mutex mutex;
    std::vector<std::pair<std::string, std::string>> events;

    EventHandler handler()
    {
        return [ this ]( const std::string& type, const std::string& json ) {
            std::lock_guard lock( mutex );
            events.emplace_back( type, json );
        };
    }

    bool has( const std::string& type, const std::string& jsonSnippet = {} )
    {
        std::lock_guard lock( mutex );
        for ( const auto& [ eventType, json ] : events ) {
            if ( eventType == type
                 && ( jsonSnippet.empty() || json.find( jsonSnippet ) != std::string::npos ) ) {
                return true;
            }
        }
        return false;
    }

    size_t count( const std::string& type )
    {
        std::lock_guard lock( mutex );
        return static_cast<size_t>(
            std::count_if( events.begin(), events.end(),
                           [ &type ]( const auto& event ) { return event.first == type; } ) );
    }

    bool waitFor( const std::string& type, const std::string& jsonSnippet = {},
                  int timeoutMs = kWaitTimeoutMs )
    {
        QElapsedTimer timer;
        timer.start();
        while ( timer.elapsed() < timeoutMs ) {
            if ( has( type, jsonSnippet ) ) {
                return true;
            }
            QThread::msleep( 10 );
        }
        return false;
    }

    // File-watch notifications are throttled (250ms) and routed through the
    // main thread, so waiting for them must pump the local event loop
    bool waitForPumping( const std::string& type, const std::string& jsonSnippet = {},
                         int timeoutMs = kWaitTimeoutMs )
    {
        QElapsedTimer timer;
        timer.start();
        while ( timer.elapsed() < timeoutMs ) {
            QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
            if ( has( type, jsonSnippet ) ) {
                return true;
            }
            QThread::msleep( 10 );
        }
        return false;
    }

    void pumpFor( int ms )
    {
        QElapsedTimer timer;
        timer.start();
        while ( timer.elapsed() < ms ) {
            QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
            QThread::msleep( 10 );
        }
    }

    void clear()
    {
        std::lock_guard lock( mutex );
        events.clear();
    }
};

void writeRawFile( const QString& path, const QByteArray& bytes )
{
    QFile out( path );
    REQUIRE( out.open( QIODevice::WriteOnly ) );
    out.write( bytes );
    out.close();
}

// Runs a synchronous session call on a side thread. A call stuck on an engine
// completion that never arrives has to fail its test in seconds instead of
// blocking the run for the engine's ten minute bound, so a timed-out thread is
// detached and left behind (the stuck call being the bug under test).
// std::nullopt means the call did not return in time; a throwing call
// rethrows through get().
template <typename T>
std::optional<Result<T>> runWithin( int timeoutMs, std::function<Result<T>()> call )
{
    std::promise<Result<T>> done;
    auto doneFuture = done.get_future();
    std::thread thread( [ &done, call = std::move( call ) ] {
        try {
            done.set_value( call() );
        }
        catch ( ... ) {
            done.set_exception( std::current_exception() );
        }
    } );

    if ( doneFuture.wait_for( std::chrono::milliseconds( timeoutMs ) )
         != std::future_status::ready ) {
        thread.detach();
        return std::nullopt;
    }

    thread.join();
    return doneFuture.get();
}

} // namespace

TEST_CASE( "API reloadFile picks up appended lines", "[api][engine]" )
{
    // Declared before the session so it outlives events fired on teardown
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    const auto file = writeTempLogFile( { "one", "two" } );
    const auto info = openSyncOrFail( *session, file.path );
    REQUIRE( info.total_lines == 2_lcount );
    REQUIRE( info.last_modified_ms > 0 );

    // Disable the watcher so the test does not race the auto-refresh reindex
    REQUIRE( session->watchFile( info.id, false ).has_value() );

    appendLines( file.path, { "three" } );

    const auto reloaded = session->reloadFile( info.id );
    REQUIRE( reloaded.has_value() );
    REQUIRE( reloaded->total_lines == 3_lcount );
    REQUIRE( reloaded->size_bytes == QFileInfo{ file.path }.size() );
    REQUIRE( reloaded->last_modified_ms >= info.last_modified_ms );

    const auto lines = session->getLines( info.id, 2_lnum, 1_lcount );
    REQUIRE( lines.has_value() );
    REQUIRE( lines->size() == 1 );
    REQUIRE( lines->at( 0 ).text == "three" );

    REQUIRE( collector.waitFor( "file.reindexed",
                                "\"fileId\":" + std::to_string( info.id ) + ",\"totalLines\":3" ) );

    const auto missing = session->reloadFile( 424242 );
    REQUIRE( !missing.has_value() );
    REQUIRE( missing.error().code == ErrorCode::FileNotOpen );
}

TEST_CASE( "API reloadFileAsync reports completion through the callback", "[api][engine]" )
{
    // Declared before the session so it outlives callbacks fired on teardown
    std::promise<Result<FileInfo>> reloadDone;
    auto reloadFuture = reloadDone.get_future();
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    const auto file = writeTempLogFile( { "one", "two" } );
    const auto info = openSyncOrFail( *session, file.path );
    REQUIRE( info.total_lines == 2_lcount );

    // Disable the watcher so the test does not race the auto-refresh reindex
    REQUIRE( session->watchFile( info.id, false ).has_value() );

    appendLines( file.path, { "three" } );

    REQUIRE( session
                 ->reloadFileAsync( info.id,
                                    [ &reloadDone ]( Result<FileInfo> result ) {
                                        reloadDone.set_value( std::move( result ) );
                                    } )
                 .has_value() );

    REQUIRE( reloadFuture.wait_for( std::chrono::milliseconds( kWaitTimeoutMs ) )
             == std::future_status::ready );
    const auto reloaded = reloadFuture.get();
    REQUIRE( reloaded.has_value() );
    REQUIRE( reloaded->total_lines == 3_lcount );

    const auto lines = session->getLines( info.id, 2_lnum, 1_lcount );
    REQUIRE( lines.has_value() );
    REQUIRE( lines->size() == 1 );
    REQUIRE( lines->at( 0 ).text == "three" );

    REQUIRE( collector.waitFor( "file.reindexed",
                                "\"fileId\":" + std::to_string( info.id ) + ",\"totalLines\":3" ) );

    const auto missing = session->reloadFileAsync( 424242, {} );
    REQUIRE( !missing.has_value() );
    REQUIRE( missing.error().code == ErrorCode::FileNotOpen );
}

TEST_CASE( "API interruptLoading aborts indexing but keeps the entry", "[api][engine]" )
{
    auto session = createSession();

    const auto file = writeTempLogFile( { "one", "two" } );
    const auto info = openSyncOrFail( *session, file.path );

    // The file finished indexing during the synchronous open, so this only
    // exercises the mapping; the entry must stay usable afterwards
    REQUIRE( session->interruptLoading( info.id ).has_value() );

    const auto after = session->fileInfo( info.id );
    REQUIRE( after.has_value() );
    REQUIRE( after->total_lines == 2_lcount );

    const auto lines = session->getLines( info.id, 0_lnum, 2_lcount );
    REQUIRE( lines.has_value() );
    REQUIRE( lines->size() == 2 );

    const auto missing = session->interruptLoading( 424242 );
    REQUIRE( !missing.has_value() );
    REQUIRE( missing.error().code == ErrorCode::FileNotOpen );
}

TEST_CASE( "API interruptLoading during indexing keeps the entry reloadable", "[api][engine]" )
{
    auto session = createSession();

    QStringList lines;
    lines.reserve( 400000 );
    for ( int i = 0; i < 400000; ++i ) {
        lines << QString( "line %1 of the interrupt test payload" ).arg( i );
    }
    const auto file = writeTempLogFile( lines );

    // Indexing runs on the engine pool: interrupt it while it is in flight
    auto opened = session->openFileAsync( file.path.toStdString(), {} );
    REQUIRE( opened.has_value() );
    REQUIRE( session->interruptLoading( opened->id ).has_value() );

    // Whether the interrupt landed mid-index or after completion, the entry
    // must stay open and reloadable
    const auto reloaded = session->reloadFile( opened->id );
    REQUIRE( reloaded.has_value() );
    REQUIRE( reloaded->total_lines == 400000_lcount );
}

TEST_CASE( "API interruptLoading during indexing emits file.indexing_failed", "[api][engine]" )
{
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    QStringList lines;
    lines.reserve( 400000 );
    for ( int i = 0; i < 400000; ++i ) {
        lines << QString( "line %1 of the interrupt event test payload" ).arg( i );
    }
    const auto file = writeTempLogFile( lines );

    // Interrupt while the 400k-line index is in flight; the dispatch
    // round-trip is orders of magnitude faster than indexing that payload
    auto opened = session->openFileAsync( file.path.toStdString(), {} );
    REQUIRE( opened.has_value() );
    REQUIRE( session->interruptLoading( opened->id ).has_value() );

    REQUIRE( collector.waitFor( "file.indexing_failed", "\"fileId\":" + std::to_string( opened->id )
                                                            + ",\"reason\":\"interrupted\"" ) );

    // A deliberate interrupt keeps the entry open and reloadable
    const auto reloaded = session->reloadFile( opened->id );
    REQUIRE( reloaded.has_value() );
    REQUIRE( reloaded->total_lines == 400000_lcount );
}

TEST_CASE( "API reloadFileAsync after an interrupt tracks its own reindex", "[api][engine]" )
{
    // Declared before the session so it outlives callbacks fired on teardown
    std::promise<Result<FileInfo>> reloadDone;
    auto reloadFuture = reloadDone.get_future();

    auto session = createSession();

    QStringList lines;
    lines.reserve( 400000 );
    for ( int i = 0; i < 400000; ++i ) {
        lines << QString( "line %1 of the async reload test payload" ).arg( i );
    }
    const auto file = writeTempLogFile( lines );

    // Interrupt the in-flight indexing, then trigger an async reload: the
    // stale Interrupted completion of the aborted index must not resolve the
    // callback, only the reload's own completion may
    auto opened = session->openFileAsync( file.path.toStdString(), {} );
    REQUIRE( opened.has_value() );
    REQUIRE( session->interruptLoading( opened->id ).has_value() );
    REQUIRE( session
                 ->reloadFileAsync( opened->id,
                                    [ &reloadDone ]( Result<FileInfo> result ) {
                                        reloadDone.set_value( std::move( result ) );
                                    } )
                 .has_value() );

    REQUIRE( reloadFuture.wait_for( std::chrono::milliseconds( kWaitTimeoutMs ) )
             == std::future_status::ready );
    const auto reloaded = reloadFuture.get();
    REQUIRE( reloaded.has_value() );
    REQUIRE( reloaded->total_lines == 400000_lcount );
}

TEST_CASE( "API same-path reopen after an interrupt restarts indexing", "[api][engine]" )
{
    // Declared before the session so they outlive callbacks fired on teardown
    std::promise<Result<FileInfo>> reopenDone;
    auto reopenFuture = reopenDone.get_future();
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    QStringList lines;
    lines.reserve( 400000 );
    for ( int i = 0; i < 400000; ++i ) {
        lines << QString( "line %1 of the reopen after interrupt payload" ).arg( i );
    }
    const auto file = writeTempLogFile( lines );

    // Interrupt the in-flight indexing and wait until the entry settles in
    // the interrupted (open but unindexed) state
    auto opened = session->openFileAsync( file.path.toStdString(), {} );
    REQUIRE( opened.has_value() );
    REQUIRE( session->interruptLoading( opened->id ).has_value() );
    REQUIRE( collector.waitFor( "file.indexing_failed", "\"fileId\":" + std::to_string( opened->id )
                                                            + ",\"reason\":\"interrupted\"" ) );

    // Reopening the same path hits the dedup entry; it must re-trigger the
    // indexing instead of returning the unindexed entry
    auto reopened = session->openFileAsync( file.path.toStdString(),
                                            [ &reopenDone ]( Result<FileInfo> result ) {
                                                reopenDone.set_value( std::move( result ) );
                                            } );
    REQUIRE( reopened.has_value() );
    REQUIRE( reopened->id == opened->id );

    REQUIRE( reopenFuture.wait_for( std::chrono::milliseconds( kWaitTimeoutMs ) )
             == std::future_status::ready );
    const auto completed = reopenFuture.get();
    REQUIRE( completed.has_value() );
    REQUIRE( completed->total_lines == 400000_lcount );
    REQUIRE( completed->indexed );

    const auto lines2 = session->getLines( opened->id, 0_lnum, 1_lcount );
    REQUIRE( lines2.has_value() );
    REQUIRE( lines2->size() == 1 );

    // Both opens share the entry, so it takes two closes to release it
    const auto openFiles = session->openFiles();
    REQUIRE( openFiles.has_value() );
    REQUIRE( openFiles->size() == 1 );
    REQUIRE( session->closeFile( opened->id ).has_value() );
    REQUIRE( session->closeFile( opened->id ).has_value() );
    const auto afterClose = session->openFiles();
    REQUIRE( afterClose.has_value() );
    REQUIRE( afterClose->empty() );
}

TEST_CASE( "API sync same-path open after an interrupt reindexes", "[api][engine]" )
{
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    QStringList lines;
    lines.reserve( 400000 );
    for ( int i = 0; i < 400000; ++i ) {
        lines << QString( "line %1 of the sync reopen after interrupt payload" ).arg( i );
    }
    const auto file = writeTempLogFile( lines );

    auto opened = session->openFileAsync( file.path.toStdString(), {} );
    REQUIRE( opened.has_value() );
    REQUIRE( session->interruptLoading( opened->id ).has_value() );
    REQUIRE( collector.waitFor( "file.indexing_failed", "\"fileId\":" + std::to_string( opened->id )
                                                            + ",\"reason\":\"interrupted\"" ) );

    // The synchronous open joins the dedup entry and blocks until the
    // re-triggered indexing completes
    const auto reopened = session->openFileSync( file.path.toStdString() );
    REQUIRE( reopened.has_value() );
    REQUIRE( reopened->id == opened->id );
    REQUIRE( reopened->total_lines == 400000_lcount );
    REQUIRE( reopened->indexed );
}

TEST_CASE( "API sync reload interrupted mid-reindex fails without hitting the timeout",
           "[api][engine]" )
{
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    const auto file = writeLargeTempLogFile();
    const auto info = openSyncOrFail( *session, file.path );

    // Only the reload's reindex may trip the progress wait below
    collector.clear();

    std::promise<Result<FileInfo>> reloadDone;
    auto reloadFuture = reloadDone.get_future();
    std::thread reloadThread(
        [ & ] { reloadDone.set_value( session->reloadFile( info.id ) ); } );

    // Interrupt only once the reload's reindex is actually running
    REQUIRE( collector.waitFor( "indexing.progress",
                                "\"fileId\":" + std::to_string( info.id ) ) );
    REQUIRE( session->interruptLoading( info.id ).has_value() );

    // A deliberate interrupt must surface right away; waiting for another
    // completion would only end on the 10-minute indexing timeout
    REQUIRE( reloadFuture.wait_for( std::chrono::milliseconds( kWaitTimeoutMs ) )
             == std::future_status::ready );
    reloadThread.join();
    const auto reloaded = reloadFuture.get();
    REQUIRE( !reloaded.has_value() );
}

TEST_CASE( "API failed same-path reopen reindex releases the shared reference", "[api][engine]" )
{
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    const auto file = writeLargeTempLogFile();

    // Interrupt the in-flight indexing and wait until the entry settles in
    // the interrupted (open but unindexed) state
    auto opened = session->openFileAsync( file.path.toStdString(), {} );
    REQUIRE( opened.has_value() );
    REQUIRE( session->interruptLoading( opened->id ).has_value() );
    REQUIRE( collector.waitFor( "file.indexing_failed", "\"fileId\":" + std::to_string( opened->id )
                                                            + ",\"reason\":\"interrupted\"" ) );

    // Only the re-triggered indexing may trip the progress wait below
    collector.clear();

    // The sync reopen joins the dedup entry (taking a second reference) and
    // re-triggers indexing; interrupt that reindex so the open fails
    std::promise<Result<FileInfo>> reopenDone;
    auto reopenFuture = reopenDone.get_future();
    std::thread reopenThread(
        [ & ] { reopenDone.set_value( session->openFileSync( file.path.toStdString() ) ); } );

    REQUIRE( collector.waitFor( "indexing.progress",
                                "\"fileId\":" + std::to_string( opened->id ) ) );
    REQUIRE( session->interruptLoading( opened->id ).has_value() );

    REQUIRE( reopenFuture.wait_for( std::chrono::milliseconds( kWaitTimeoutMs ) )
             == std::future_status::ready );
    reopenThread.join();
    const auto reopened = reopenFuture.get();
    REQUIRE( !reopened.has_value() );

    // The failed open must have released its reference: the original opener's
    // single close destroys the entry
    REQUIRE( session->closeFile( opened->id ).has_value() );
    const auto afterClose = session->openFiles();
    REQUIRE( afterClose.has_value() );
    REQUIRE( afterClose->empty() );

    const auto gone = session->fileInfo( opened->id );
    REQUIRE( !gone.has_value() );
}

TEST_CASE( "API search progress callback can register another callback", "[api][engine]" )
{
    // Declared before the session so they outlive callbacks fired on teardown
    std::atomic<int> firstCalls{ 0 };
    std::atomic<int> secondCalls{ 0 };

    auto session = createSession();

    // Big enough for several throttled progress emissions
    const auto file = writeLargeTempLogFile();
    const auto info = openSyncOrFail( *session, file.path );

    SearchParams params;
    params.pattern = "match";
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );

    session->onSearchProgress( *search, [ & ]( SearchId id, LinesCount, float ) {
        ++firstCalls;
        // Registering from inside the dispatch appends to the vector being
        // iterated; the snapshot must keep that safe
        session->onSearchProgress( id,
                                   [ & ]( SearchId, LinesCount, float ) { ++secondCalls; } );
    } );

    REQUIRE( waitForSearchDone( *session, *search ) );
    REQUIRE( firstCalls > 0 );
    REQUIRE( secondCalls > 0 );
}

TEST_CASE( "API watchFile emits file.changed only while watching", "[api][engine]" )
{
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    const auto file = writeTempLogFile( { "w0" } );
    const auto info = openSyncOrFail( *session, file.path );

    // Watching is on by default (the engine registers the file after
    // indexing); make it explicit
    REQUIRE( session->watchFile( info.id, true ).has_value() );

    appendLines( file.path, { "w1" } );
    REQUIRE( collector.waitForPumping( "file.changed", "\"fileId\":" + std::to_string( info.id )
                                                           + ",\"reason\":\"modified\"" ) );

    REQUIRE( session->watchFile( info.id, false ).has_value() );

    // Drain in-flight notifications of the first append before snapshotting
    collector.pumpFor( 1000 );
    const auto changedBefore = collector.count( "file.changed" );

    appendLines( file.path, { "w2" } );
    collector.pumpFor( 3000 );
    REQUIRE( collector.count( "file.changed" ) == changedBefore );

    const auto missing = session->watchFile( 424242, true );
    REQUIRE( !missing.has_value() );
    REQUIRE( missing.error().code == ErrorCode::FileNotOpen );
}

TEST_CASE( "API checkFileChanges reindexes without the watcher", "[api][engine]" )
{
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    const auto file = writeTempLogFile( { "one", "two" } );
    const auto info = openSyncOrFail( *session, file.path );
    REQUIRE( session->watchFile( info.id, false ).has_value() );

    appendLines( file.path, { "three" } );
    collector.pumpFor( 1000 );
    REQUIRE( collector.count( "file.reindexed" ) == 0 );

    REQUIRE( session->checkFileChanges( info.id ).has_value() );
    REQUIRE( collector.waitForPumping( "file.reindexed",
                                       "\"fileId\":" + std::to_string( info.id )
                                           + ",\"totalLines\":3" ) );
    REQUIRE( collector.has( "file.reindexed", "\"sizeBytes\":" ) );
    REQUIRE( collector.has( "file.reindexed", "\"lastModifiedMs\":" ) );

    const auto refreshed = session->fileInfo( info.id );
    REQUIRE( refreshed.has_value() );
    REQUIRE( refreshed->total_lines == 3_lcount );
}

TEST_CASE( "API extendSearch scans only appended lines", "[api][engine]" )
{
    EventCollector collector;
    auto session = createSession();
    session->subscribe( collector.handler() );
    const auto file = writeTempLogFile( { "one", "two", "one" } );
    const auto info = openSyncOrFail( *session, file.path );

    SearchParams params;
    params.pattern = "one";
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );
    REQUIRE( waitForSearchDone( *session, *search ) );

    auto progress = session->searchProgress( *search );
    REQUIRE( progress.has_value() );
    REQUIRE( progress->total_matches == 2_lcount );

    appendLines( file.path, { "one appended" } );
    REQUIRE( session->checkFileChanges( info.id ).has_value() );
    REQUIRE( collector.waitForPumping( "file.reindexed",
                                       "\"fileId\":" + std::to_string( info.id ) ) );
    REQUIRE( session->extendSearch( *search, 4_lnum ).has_value() );

    QElapsedTimer timer;
    timer.start();
    while ( timer.elapsed() < kWaitTimeoutMs ) {
        progress = session->searchProgress( *search );
        REQUIRE( progress.has_value() );
        if ( progress->progress >= 1.0f && progress->total_matches == 3_lcount ) {
            break;
        }
        QThread::msleep( 10 );
    }
    REQUIRE( progress->progress >= 1.0f );
    REQUIRE( progress->total_matches == 3_lcount );

    const auto matches = session->searchMatches( *search, 0, 10 );
    REQUIRE( matches.has_value() );
    REQUIRE( matches->size() == 3 );
    REQUIRE( matches->back() == 3_lnum );
}

TEST_CASE( "API extendSearch without new lines is a no-op", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "one", "two", "one" } );
    const auto info = openSyncOrFail( *session, file.path );

    SearchParams params;
    params.pattern = "one";
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );
    REQUIRE( waitForSearchDone( *session, *search ) );

    REQUIRE( session->extendSearch( *search, 3_lnum ).has_value() );
    const auto progress = session->searchProgress( *search );
    REQUIRE( progress.has_value() );
    REQUIRE( progress->total_matches == 2_lcount );
    REQUIRE( progress->progress >= 1.0f );
}

TEST_CASE( "API extendSearch stops at maxResults", "[api][engine]" )
{
    EventCollector collector;
    auto session = createSession();
    session->subscribe( collector.handler() );
    const auto file = writeTempLogFile( { "one", "two", "one" } );
    const auto info = openSyncOrFail( *session, file.path );

    SearchParams params;
    params.pattern = "one";
    params.maxResults = 1;
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );
    REQUIRE( waitForSearchDone( *session, *search ) );

    const auto initial = session->searchProgress( *search );
    REQUIRE( initial.has_value() );

    appendLines( file.path, { "one appended" } );
    REQUIRE( session->checkFileChanges( info.id ).has_value() );
    REQUIRE( collector.waitForPumping( "file.reindexed",
                                       "\"fileId\":" + std::to_string( info.id ) ) );

    REQUIRE( session->extendSearch( *search, 4_lnum ).has_value() );
    collector.pumpFor( 500 );
    const auto progress = session->searchProgress( *search );
    REQUIRE( progress.has_value() );
    // The chunk that reached maxResults may have reported more than one match;
    // what matters is that extension does not grow the result set further.
    REQUIRE( progress->total_matches == initial->total_matches );
}

TEST_CASE( "API extendSearch keeps order and max length across repeated growth",
           "[api][engine]" )
{
    EventCollector collector;
    auto session = createSession();
    session->subscribe( collector.handler() );
    const auto file = writeTempLogFile( { "needle", "other" } );
    const auto info = openSyncOrFail( *session, file.path );

    SearchParams params;
    params.pattern = "needle";
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );
    REQUIRE( waitForSearchDone( *session, *search ) );

    const auto waitForMatches = [ & ]( LinesCount expected ) {
        QElapsedTimer timer;
        timer.start();
        while ( timer.elapsed() < kWaitTimeoutMs ) {
            const auto progress = session->searchProgress( *search );
            REQUIRE( progress.has_value() );
            if ( progress->progress >= 1.0f && progress->total_matches == expected ) {
                return true;
            }
            QThread::msleep( 10 );
        }
        return false;
    };

    appendLines( file.path, { "needle two" } );
    REQUIRE( session->checkFileChanges( info.id ).has_value() );
    REQUIRE( collector.waitForPumping( "file.reindexed",
                                       "\"fileId\":" + std::to_string( info.id )
                                           + ",\"totalLines\":3" ) );
    REQUIRE( session->extendSearch( *search, 3_lnum ).has_value() );
    REQUIRE( waitForMatches( 2_lcount ) );

    const QString longTail = QStringLiteral( "needle " ) + QString( 5000, QLatin1Char( 'x' ) );
    appendLines( file.path, { longTail } );
    REQUIRE( session->checkFileChanges( info.id ).has_value() );
    REQUIRE( collector.waitForPumping( "file.reindexed",
                                       "\"fileId\":" + std::to_string( info.id )
                                           + ",\"totalLines\":4" ) );
    REQUIRE( session->extendSearch( *search, 4_lnum ).has_value() );
    REQUIRE( waitForMatches( 3_lcount ) );

    const auto matches = session->searchMatches( *search, 0, 10 );
    REQUIRE( matches.has_value() );
    REQUIRE( matches->size() == 3 );
    REQUIRE( matches->at( 0 ) == 0_lnum );
    REQUIRE( matches->at( 1 ) == 2_lnum );
    REQUIRE( matches->at( 2 ) == 3_lnum );

    const auto maxLength = session->filteredMaxLineLength( *search );
    REQUIRE( maxLength.has_value() );
    REQUIRE( maxLength->get() >= 5000 );
}

TEST_CASE( "API auto encoding appends trigger a single reindex", "[api][engine]" )
{
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    const auto file = writeTempLogFile( { "one", "two" } );
    const auto info = openSyncOrFail( *session, file.path );
    REQUIRE( session->watchFile( info.id, true ).has_value() );

    appendLines( file.path, { "three" } );

    // The append reindexes once; the encoding re-detection of that reindex
    // matches the current codec and must not trigger a second one
    REQUIRE( collector.waitForPumping( "file.reindexed", "\"fileId\":" + std::to_string( info.id )
                                                             + ",\"totalLines\":3" ) );
    collector.pumpFor( 2000 );
    REQUIRE( collector.count( "file.reindexed" ) == 1 );
}

TEST_CASE( "API auto encoding tracks a codec change on rewrite", "[api][engine]" )
{
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    const auto file = writeTempLogFile( { "alpha", "beta" } );
    const auto info = openSyncOrFail( *session, file.path );
    REQUIRE( session->watchFile( info.id, true ).has_value() );

    // Rewrite the file in UTF-16LE: the natural reindex re-detects the codec
    {
        const auto* codec = QTextCodec::codecForName( "UTF-16LE" );
        REQUIRE( codec );
        QFile out( file.path );
        REQUIRE( out.open( QIODevice::WriteOnly ) );
        out.write( "\xFF\xFE", 2 );
        out.write( codec->fromUnicode( QStringLiteral( "gamma\ndelta\n" ) ) );
    }

    REQUIRE(
        collector.waitForPumping( "file.reindexed", "\"fileId\":" + std::to_string( info.id ) ) );

    // fileInfo must report the re-detected codec so an encoding re-application
    // (the UI's updateEncoding) no-ops instead of reindexing with the stale one
    const auto refreshed = session->fileInfo( info.id );
    REQUIRE( refreshed.has_value() );
    INFO( "encoding after rewrite: " + refreshed->encoding.name );
    REQUIRE( refreshed->encoding.name == "UTF-16LE" );

    REQUIRE( session->setEncodingAsync( info.id, refreshed->encoding.name, {} ).has_value() );
    collector.pumpFor( 2000 );
    REQUIRE( collector.count( "file.reindexed" ) == 1 );

    const auto decoded = session->getLines( info.id, 0_lnum, 1_lcount );
    REQUIRE( decoded.has_value() );
    REQUIRE( decoded->at( 0 ).text.find( "gamma" ) != std::string::npos );
}

TEST_CASE( "API setEncoding reinterprets the file content", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempUtf16LeFile( { "alpha", "beta" } );

    const auto info = openSyncOrFail( *session, file.path );
    INFO( "detected encoding: " + info.encoding.name );
    REQUIRE( info.encoding.name == "UTF-16LE" );

    const auto decoded = session->getLines( info.id, 0_lnum, 1_lcount );
    REQUIRE( decoded.has_value() );
    REQUIRE( decoded->size() == 1 );
    REQUIRE( decoded->at( 0 ).text.find( "alpha" ) != std::string::npos );

    // A wrong interpretation garbles the text
    REQUIRE( session->setEncoding( info.id, "UTF-8" ).has_value() );
    const auto garbled = session->getLines( info.id, 0_lnum, 1_lcount );
    REQUIRE( garbled.has_value() );
    REQUIRE( garbled->at( 0 ).text.find( "alpha" ) == std::string::npos );

    const auto reencoded = session->fileInfo( info.id );
    REQUIRE( reencoded.has_value() );
    REQUIRE( reencoded->encoding.name == "UTF-8" );

    // And back to the right one
    REQUIRE( session->setEncoding( info.id, "UTF-16LE" ).has_value() );
    const auto restored = session->getLines( info.id, 0_lnum, 1_lcount );
    REQUIRE( restored.has_value() );
    REQUIRE( restored->at( 0 ).text.find( "alpha" ) != std::string::npos );

    const auto bad = session->setEncoding( info.id, "klogg-no-such-codec" );
    REQUIRE( !bad.has_value() );
    REQUIRE( bad.error().code == ErrorCode::EncodingError );

    const auto missing = session->setEncoding( 424242, "UTF-8" );
    REQUIRE( !missing.has_value() );
    REQUIRE( missing.error().code == ErrorCode::FileNotOpen );
}

TEST_CASE( "API setEncodingAsync reinterprets the file content", "[api][engine]" )
{
    // Declared before the session so they outlive callbacks fired on teardown
    std::promise<Result<FileInfo>> reindexed;
    auto reindexedFuture = reindexed.get_future();
    std::promise<Result<FileInfo>> applied;
    auto appliedFuture = applied.get_future();

    auto session = createSession();
    const auto file = writeTempUtf16LeFile( { "alpha", "beta" } );

    const auto info = openSyncOrFail( *session, file.path );
    INFO( "detected encoding: " + info.encoding.name );
    REQUIRE( info.encoding.name == "UTF-16LE" );

    // A mib change triggers an asynchronous reindex; the callback fires with
    // the refreshed FileInfo once it completes
    REQUIRE( session
                 ->setEncodingAsync( info.id, "UTF-8",
                                     [ &reindexed ]( Result<FileInfo> result ) {
                                         reindexed.set_value( std::move( result ) );
                                     } )
                 .has_value() );

    REQUIRE( reindexedFuture.wait_for( std::chrono::milliseconds( kWaitTimeoutMs ) )
             == std::future_status::ready );
    const auto reencoded = reindexedFuture.get();
    REQUIRE( reencoded.has_value() );
    REQUIRE( reencoded->encoding.name == "UTF-8" );

    const auto garbled = session->getLines( info.id, 0_lnum, 1_lcount );
    REQUIRE( garbled.has_value() );
    REQUIRE( garbled->at( 0 ).text.find( "alpha" ) == std::string::npos );

    // Re-applying the current codec needs no reindex: the callback fires
    // before the call even returns
    REQUIRE( session
                 ->setEncodingAsync( info.id, "UTF-8",
                                     [ &applied ]( Result<FileInfo> result ) {
                                         applied.set_value( std::move( result ) );
                                     } )
                 .has_value() );
    REQUIRE( appliedFuture.wait_for( std::chrono::milliseconds( kWaitTimeoutMs ) )
             == std::future_status::ready );
    REQUIRE( appliedFuture.get().has_value() );

    const auto bad = session->setEncodingAsync( info.id, "klogg-no-such-codec", {} );
    REQUIRE( !bad.has_value() );
    REQUIRE( bad.error().code == ErrorCode::EncodingError );

    const auto missing = session->setEncodingAsync( 424242, "UTF-8", {} );
    REQUIRE( !missing.has_value() );
    REQUIRE( missing.error().code == ErrorCode::FileNotOpen );
}

// An explicit encoding that differs from the detection but shares the index's
// line-feed geometry needs no reindex: the open must complete on the encoding
// application itself rather than wait for a completion the engine never emits
TEST_CASE( "API open with an explicit 8-bit encoding completes without a reindex",
           "[api][engine]" )
{
    // Pure ASCII leaves the detector without a guess: it falls back to
    // ISO-8859-1, so an explicit UTF-8 differs from it
    const auto file = writeTempLogFile( { "one", "two", "three" } );

    auto session = createSession();
    const auto probed = openSyncOrFail( *session, file.path );
    INFO( "detected encoding: " + probed.encoding.name );
    REQUIRE( probed.encoding.name == "ISO-8859-1" );
    REQUIRE( session->closeFile( probed.id ).has_value() );

    // The asynchronous form must report the applied encoding
    std::promise<Result<FileInfo>> opened;
    auto openedFuture = opened.get_future();
    const auto pending = session->openFileAsync( file.path.toStdString(),
                                                [ &opened ]( Result<FileInfo> result ) {
                                                    opened.set_value( std::move( result ) );
                                                },
                                                std::string{ "UTF-8" } );
    REQUIRE( pending.has_value() );
    INFO( "openFileAsync with an explicit encoding must complete on its own" );
    REQUIRE( openedFuture.wait_for( std::chrono::milliseconds( kCompletionTimeoutMs ) )
             == std::future_status::ready );
    const auto asyncInfo = openedFuture.get();
    REQUIRE( asyncInfo.has_value() );
    REQUIRE( asyncInfo->indexed );
    REQUIRE( asyncInfo->total_lines == 3_lcount );
    REQUIRE( asyncInfo->encoding.name == "UTF-8" );
    REQUIRE( session->closeFile( pending->id ).has_value() );

    // And so must the synchronous form
    const auto syncInfo = runWithin<FileInfo>( kCompletionTimeoutMs, [ & ] {
        return session->openFileSync( file.path.toStdString(), std::string{ "UTF-8" } );
    } );
    INFO( "openFileSync with an explicit encoding must not hit the indexing timeout" );
    REQUIRE( syncInfo.has_value() );
    REQUIRE( syncInfo->has_value() );
    REQUIRE( ( *syncInfo )->indexed );
    REQUIRE( ( *syncInfo )->total_lines == 3_lcount );
    REQUIRE( ( *syncInfo )->encoding.name == "UTF-8" );

    // The geometry the index holds stays valid, so the content is readable
    const auto lines = session->getLines( ( *syncInfo )->id, 0_lnum, 3_lcount );
    REQUIRE( lines.has_value() );
    REQUIRE( lines->size() == 3 );
    REQUIRE( lines->at( 0 ).text == "one" );
}

// The two remaining open paths: an explicit encoding equal to the detection
// (nothing reindexes either) and one that changes the line feed geometry, in
// which case the open may only report once the triggered reindex landed
TEST_CASE( "API open with an explicit matching or reindexing encoding completes",
           "[api][engine]" )
{
    const auto file = writeTempLogFile( { "one", "two", "three" } );

    auto session = createSession();

    const auto matching = runWithin<FileInfo>( kCompletionTimeoutMs, [ & ] {
        return session->openFileSync( file.path.toStdString(), std::string{ "ISO-8859-1" } );
    } );
    REQUIRE( matching.has_value() );
    REQUIRE( matching->has_value() );
    REQUIRE( ( *matching )->indexed );
    REQUIRE( ( *matching )->encoding.name == "ISO-8859-1" );
    REQUIRE( ( *matching )->total_lines == 3_lcount );
    REQUIRE( session->closeFile( ( *matching )->id ).has_value() );

    // UTF-16 line feeds are two bytes wide, so applying that codec reindexes;
    // the single byte line feeds of the ASCII content are gone and the whole
    // file becomes one line
    const auto reindexed = runWithin<FileInfo>( kCompletionTimeoutMs, [ & ] {
        return session->openFileSync( file.path.toStdString(), std::string{ "UTF-16LE" } );
    } );
    REQUIRE( reindexed.has_value() );
    REQUIRE( reindexed->has_value() );
    REQUIRE( ( *reindexed )->indexed );
    REQUIRE( ( *reindexed )->encoding.name == "UTF-16LE" );
    REQUIRE( ( *reindexed )->total_lines == 1_lcount );
}

// A configured default codec indexes the file under it, while the detection
// still guesses the codec the content really is: opening such a file syncs the
// display codec to the guess, which reindexes back to auto-detection, and the
// open may only report the geometry of that final index
TEST_CASE( "API open with a default forced codec completes on the detected index",
           "[api][engine]" )
{
    const auto file = writeTempLogFile( { "one", "two", "three" } );

    EngineOptions options;
    options.defaultEncodingMib = 1014; // UTF-16LE: a geometry change on ASCII
    auto session = createSession( options );

    const auto info = runWithin<FileInfo>( kCompletionTimeoutMs, [ & ] {
        return session->openFileSync( file.path.toStdString() );
    } );
    INFO( "the open must wait for the reindex back to the detected codec" );
    REQUIRE( info.has_value() );
    REQUIRE( info->has_value() );
    REQUIRE( ( *info )->indexed );
    REQUIRE( ( *info )->encoding.name == "ISO-8859-1" );
    REQUIRE( ( *info )->total_lines == 3_lcount );

    const auto lines = session->getLines( ( *info )->id, 0_lnum, 3_lcount );
    REQUIRE( lines.has_value() );
    REQUIRE( lines->size() == 3 );
    REQUIRE( lines->at( 0 ).text == "one" );
}

TEST_CASE( "API setEncoding between equal line-feed codecs applies without a reindex",
           "[api][engine]" )
{
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    const auto file = writeTempLogFile( { "one", "two", "three" } );
    const auto info = openSyncOrFail( *session, file.path );
    INFO( "detected encoding: " + info.encoding.name );
    REQUIRE( info.encoding.name == "ISO-8859-1" );

    // Only the encoding changes below may emit indexing events
    collector.clear();

    // UTF-8 shares the single-byte line-feed geometry of the detection, so the
    // index stays valid and the reindex-free application must be reported
    std::promise<Result<FileInfo>> applied;
    auto appliedFuture = applied.get_future();
    REQUIRE( session
                 ->setEncodingAsync( info.id, "UTF-8",
                                     [ &applied ]( Result<FileInfo> result ) {
                                         applied.set_value( std::move( result ) );
                                     } )
                 .has_value() );
    INFO( "setEncodingAsync must report a reindex-free encoding change" );
    REQUIRE( appliedFuture.wait_for( std::chrono::milliseconds( kCompletionTimeoutMs ) )
             == std::future_status::ready );
    const auto appliedInfo = appliedFuture.get();
    REQUIRE( appliedInfo.has_value() );
    REQUIRE( appliedInfo->encoding.name == "UTF-8" );

    // The synchronous form returns on its own as well
    const auto syncResult = runWithin<void>(
        kCompletionTimeoutMs, [ & ] { return session->setEncoding( info.id, "ISO-8859-1" ); } );
    INFO( "setEncoding must not hit the indexing timeout" );
    REQUIRE( syncResult.has_value() );
    REQUIRE( ( *syncResult ).has_value() );

    const auto refreshed = session->fileInfo( info.id );
    REQUIRE( refreshed.has_value() );
    REQUIRE( refreshed->encoding.name == "ISO-8859-1" );

    // Neither change reindexes: the index only depends on the line-feed
    // geometry, which both codecs share
    REQUIRE( collector.count( "file.reindexed" ) == 0 );
    REQUIRE( collector.count( "file.indexed" ) == 0 );

    const auto lines = session->getLines( info.id, 0_lnum, 3_lcount );
    REQUIRE( lines.has_value() );
    REQUIRE( lines->size() == 3 );
}

TEST_CASE( "API restoreSession reopens a file with its saved encoding", "[api][engine]" )
{
    // A UTF-8 byte order mark pins the detection, so a plain open records
    // UTF-8 in the session json; the same content without the mark then
    // detects as ISO-8859-1, the mismatch a restored session opens through
    TempLogFile file;
    file.path = tempFilePath();
    writeRawFile( file.path, QByteArray( "\xEF\xBB\xBFone\ntwo\nthree\n" ) );

    std::string savedSession;
    {
        auto session = createSession();
        const auto info = openSyncOrFail( *session, file.path );
        INFO( "detected encoding: " + info.encoding.name );
        REQUIRE( info.encoding.name == "UTF-8" );

        const auto saved = session->saveSession();
        REQUIRE( saved.has_value() );
        savedSession = *saved;
    }

    writeRawFile( file.path, QByteArray( "one\ntwo\nthree\n" ) );

    auto session = createSession();
    const auto restored = runWithin<void>(
        kCompletionTimeoutMs, [ & ] { return session->restoreSession( savedSession ); } );
    INFO( "restoreSession must not hang on the saved encoding of a file" );
    REQUIRE( restored.has_value() );
    REQUIRE( ( *restored ).has_value() );

    const auto files = session->openFiles();
    REQUIRE( files.has_value() );
    REQUIRE( files->size() == 1 );
    REQUIRE( files->at( 0 ).encoding.name == "UTF-8" );
    REQUIRE( files->at( 0 ).indexed );
    REQUIRE( files->at( 0 ).total_lines == 3_lcount );
}

TEST_CASE( "API supportedEncodings returns a flat codec name list", "[api][engine]" )
{
    auto session = createSession();
    const auto encodings = session->supportedEncodings();
    REQUIRE( encodings.has_value() );
    REQUIRE( !encodings->empty() );
    REQUIRE( std::find( encodings->begin(), encodings->end(), "UTF-8" ) != encodings->end() );
}

TEST_CASE( "API searchMatchIndex maps a line to the first match at or after it", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "INFO alpha", "WARN beta", "INFO gamma", "WARN delta" } );
    const auto info = openSyncOrFail( *session, file.path );

    SearchParams params;
    params.pattern = "WARN";
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );
    REQUIRE( waitForSearchDone( *session, *search ) );

    // Matches are at lines 1 and 3
    const auto exactHit = session->searchMatchIndex( *search, 1_lnum );
    REQUIRE( exactHit.has_value() );
    REQUIRE( exactHit->has_value() );
    REQUIRE( **exactHit == 0 );

    const auto between = session->searchMatchIndex( *search, 2_lnum );
    REQUIRE( between.has_value() );
    REQUIRE( between->has_value() );
    REQUIRE( **between == 1 );

    const auto beforeAll = session->searchMatchIndex( *search, 0_lnum );
    REQUIRE( beforeAll.has_value() );
    REQUIRE( beforeAll->has_value() );
    REQUIRE( **beforeAll == 0 );

    const auto pastEnd = session->searchMatchIndex( *search, 4_lnum );
    REQUIRE( pastEnd.has_value() );
    REQUIRE( !pastEnd->has_value() );

    REQUIRE( session->cancelSearch( *search ).has_value() );
    const auto cancelled = session->searchMatchIndex( *search, 1_lnum );
    REQUIRE( !cancelled.has_value() );
    REQUIRE( cancelled.error().code == ErrorCode::SearchCancelled );

    const auto unknown = session->searchMatchIndex( 424242, 0_lnum );
    REQUIRE( !unknown.has_value() );
    REQUIRE( unknown.error().code == ErrorCode::InvalidSearchId );
}

TEST_CASE( "API getFilteredLines returns the filtered view", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "INFO alpha", "WARN beta", "INFO gamma", "WARN delta" } );
    const auto info = openSyncOrFail( *session, file.path );

    SearchParams params;
    params.pattern = "WARN";
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );
    REQUIRE( waitForSearchDone( *session, *search ) );

    const auto all = session->getFilteredLines( *search, 0, 10 );
    REQUIRE( all.has_value() );
    REQUIRE( all->size() == 2 );
    REQUIRE( all->at( 0 ).num == 1_lnum );
    REQUIRE( all->at( 0 ).text == "WARN beta" );
    REQUIRE( all->at( 1 ).num == 3_lnum );
    REQUIRE( all->at( 1 ).text == "WARN delta" );

    const auto clamped = session->getFilteredLines( *search, 1, 10 );
    REQUIRE( clamped.has_value() );
    REQUIRE( clamped->size() == 1 );
    REQUIRE( clamped->at( 0 ).num == 3_lnum );

    const auto beyond = session->getFilteredLines( *search, 2, 1 );
    REQUIRE( beyond.has_value() );
    REQUIRE( beyond->empty() );

    const auto unknown = session->getFilteredLines( 424242, 0, 1 );
    REQUIRE( !unknown.has_value() );
    REQUIRE( unknown.error().code == ErrorCode::InvalidSearchId );
}

TEST_CASE( "API getFilteredLines applies the ANSI modes", "[api][engine]" )
{
    auto session = createSession();
    const auto file
        = writeTempLogFile( { QString::fromUtf8( "\x1b[31mred alert\x1b[0m" ), "quiet" } );
    const auto info = openSyncOrFail( *session, file.path );

    SearchParams params;
    params.pattern = "alert";
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );
    REQUIRE( waitForSearchDone( *session, *search ) );

    const auto raw = session->getFilteredLines( *search, 0, 10, AnsiMode::Raw );
    REQUIRE( raw.has_value() );
    REQUIRE( raw->size() == 1 );
    REQUIRE( raw->at( 0 ).text.find( '\x1b' ) != std::string::npos );

    const auto stripped = session->getFilteredLines( *search, 0, 10, AnsiMode::Strip );
    REQUIRE( stripped.has_value() );
    REQUIRE( stripped->at( 0 ).text == "red alert" );

    const auto segmented = session->getFilteredLines( *search, 0, 10, AnsiMode::Segments );
    REQUIRE( segmented.has_value() );
    REQUIRE( segmented->at( 0 ).text == "red alert" );
    REQUIRE( !segmented->at( 0 ).segments.empty() );
}

TEST_CASE( "API markAfter and markBefore navigate the marks", "[api][engine]" )
{
    auto session = createSession();
    const auto file
        = writeTempLogFile( { "l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7", "l8", "l9" } );
    const auto info = openSyncOrFail( *session, file.path );

    const auto emptyAfter = session->markAfter( info.id, 5_lnum );
    REQUIRE( emptyAfter.has_value() );
    REQUIRE( !emptyAfter->has_value() );
    const auto emptyBefore = session->markBefore( info.id, 5_lnum );
    REQUIRE( emptyBefore.has_value() );
    REQUIRE( !emptyBefore->has_value() );

    REQUIRE( session->addMarks( info.id, { 2_lnum, 5_lnum, 8_lnum } ).has_value() );

    const auto after = session->markAfter( info.id, 5_lnum );
    REQUIRE( after.has_value() );
    REQUIRE( after->has_value() );
    REQUIRE( **after == 8_lnum );

    const auto afterGap = session->markAfter( info.id, 3_lnum );
    REQUIRE( afterGap.has_value() );
    REQUIRE( afterGap->has_value() );
    REQUIRE( **afterGap == 5_lnum );

    const auto afterLast = session->markAfter( info.id, 8_lnum );
    REQUIRE( afterLast.has_value() );
    REQUIRE( !afterLast->has_value() );

    const auto before = session->markBefore( info.id, 5_lnum );
    REQUIRE( before.has_value() );
    REQUIRE( before->has_value() );
    REQUIRE( **before == 2_lnum );

    const auto beforeGap = session->markBefore( info.id, 7_lnum );
    REQUIRE( beforeGap.has_value() );
    REQUIRE( beforeGap->has_value() );
    REQUIRE( **beforeGap == 5_lnum );

    const auto beforeFirst = session->markBefore( info.id, 2_lnum );
    REQUIRE( beforeFirst.has_value() );
    REQUIRE( !beforeFirst->has_value() );

    const auto unknown = session->markAfter( 424242, 0_lnum );
    REQUIRE( !unknown.has_value() );
    REQUIRE( unknown.error().code == ErrorCode::FileNotOpen );
}

TEST_CASE( "API marks mutations emit marks.changed", "[api][engine]" )
{
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    const auto file = writeTempLogFile( { "l0", "l1", "l2", "l3" } );
    const auto info = openSyncOrFail( *session, file.path );
    const auto payload = "\"fileId\":" + std::to_string( info.id );

    REQUIRE( session->addMarks( info.id, { 1_lnum, 3_lnum } ).has_value() );
    REQUIRE( collector.waitFor( "marks.changed", payload ) );

    REQUIRE( session->removeMarks( info.id, { 1_lnum } ).has_value() );
    REQUIRE( collector.waitFor( "marks.changed", payload ) );
    REQUIRE( collector.count( "marks.changed" ) == 2 );

    // A mutation of an unknown file reports no event
    REQUIRE( !session->addMarks( 424242, { 0_lnum } ).has_value() );
    REQUIRE( collector.count( "marks.changed" ) == 2 );
}

TEST_CASE( "API file.reindexed handler may call the write API", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "one", "two" } );
    const auto info = openSyncOrFail( *session, file.path );

    std::atomic<bool> markApplied{ false };
    session->subscribe( [ & ]( const std::string& type, const std::string& ) {
        if ( type != "file.reindexed" ) {
            return;
        }
        // Handlers run synchronously on the session worker, inside the engine
        // callback: a write call from here runs inline and must not meet the
        // file's field lock still held by the emitter
        markApplied = session->addMarks( info.id, { 1_lnum } ).has_value();
    } );

    REQUIRE( session->reloadFile( info.id ).has_value() );
    REQUIRE( markApplied.load() );

    const auto marks = session->listMarks( info.id );
    REQUIRE( marks.has_value() );
    REQUIRE( marks->size() == 1 );
    REQUIRE( marks->at( 0 ) == 1_lnum );
}

TEST_CASE( "API session destroyed from its own worker thread still tears down", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "one", "two", "three" } );

    std::atomic<bool> destroyed{ false };
    session->subscribe( [ & ]( const std::string& type, const std::string& ) {
        if ( type == "file.indexed" ) {
            // Destroying a session from one of its own callbacks is
            // unsupported; the destructor must refuse to join the calling
            // worker thread instead of terminating the process
            session.reset();
            destroyed = true;
        }
    } );

    const auto opened = session->openFileAsync( file.path.toStdString(), {} );
    REQUIRE( opened.has_value() );

    QElapsedTimer timer;
    timer.start();
    while ( !destroyed.load() && timer.elapsed() < kWaitTimeoutMs ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
        QThread::msleep( 10 );
    }
    REQUIRE( destroyed.load() );
}

TEST_CASE( "API same-path opens share one file entry", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "one", "two", "three" } );

    const auto first = openSyncOrFail( *session, file.path );
    const auto second = session->openFileSync( file.path.toStdString() );
    REQUIRE( second.has_value() );
    REQUIRE( second->id == first.id );
    REQUIRE( second->indexed );
    REQUIRE( second->total_lines == 3_lcount );

    const auto files = session->openFiles();
    REQUIRE( files.has_value() );
    REQUIRE( files->size() == 1 );

    REQUIRE( session->closeFile( first.id ).has_value() );
    REQUIRE( session->closeFile( first.id ).has_value() );
}

TEST_CASE( "API path spellings resolving to the same file share the entry", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "one", "two" } );

    const auto first = openSyncOrFail( *session, file.path );
    const auto dottedPath
        = QString( "%1/./%2" ).arg( QDir::tempPath(), QFileInfo( file.path ).fileName() );
    const auto second = session->openFileSync( dottedPath.toStdString() );
    REQUIRE( second.has_value() );
    REQUIRE( second->id == first.id );

    REQUIRE( session->closeFile( first.id ).has_value() );
    REQUIRE( session->closeFile( first.id ).has_value() );
}

TEST_CASE( "API different paths get different FileIds", "[api][engine]" )
{
    auto session = createSession();
    const auto firstFile = writeTempLogFile( { "a" } );
    const auto secondFile = writeTempLogFile( { "b" } );

    const auto first = openSyncOrFail( *session, firstFile.path );
    const auto second = openSyncOrFail( *session, secondFile.path );
    REQUIRE( first.id != second.id );

    REQUIRE( session->closeFile( first.id ).has_value() );
    REQUIRE( session->closeFile( second.id ).has_value() );
}

TEST_CASE( "API closeFile applies reference counting", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "one", "two" } );

    const auto info = openSyncOrFail( *session, file.path );
    REQUIRE( session->openFileSync( file.path.toStdString() ).has_value() );

    // The first close only drops a reference: the file stays usable
    REQUIRE( session->closeFile( info.id ).has_value() );
    const auto lines = session->getLines( info.id, 0_lnum, 2_lcount );
    REQUIRE( lines.has_value() );
    REQUIRE( lines->size() == 2 );

    // The second close destroys the entry
    REQUIRE( session->closeFile( info.id ).has_value() );
    const auto afterClose = session->getLines( info.id, 0_lnum, 1_lcount );
    REQUIRE( !afterClose.has_value() );
    REQUIRE( afterClose.error().code == ErrorCode::FileNotOpen );

    // A fully closed id is unknown
    const auto thirdClose = session->closeFile( info.id );
    REQUIRE( !thirdClose.has_value() );
    REQUIRE( thirdClose.error().code == ErrorCode::InvalidFileId );
}

TEST_CASE( "API async same-path open joins the in-flight indexing", "[api][engine]" )
{
    // Declared before the session so they outlive callbacks fired on teardown
    std::promise<Result<FileInfo>> firstDone;
    std::promise<Result<FileInfo>> secondDone;
    auto firstFuture = firstDone.get_future();
    auto secondFuture = secondDone.get_future();

    auto session = createSession();
    const auto file = writeTempLogFile( { "one", "two", "three" } );

    const auto first = session->openFileAsync(
        file.path.toStdString(),
        [ &firstDone ]( Result<FileInfo> result ) { firstDone.set_value( std::move( result ) ); } );
    REQUIRE( first.has_value() );

    const auto second = session->openFileAsync( file.path.toStdString(),
                                                [ &secondDone ]( Result<FileInfo> result ) {
                                                    secondDone.set_value( std::move( result ) );
                                                } );
    REQUIRE( second.has_value() );
    REQUIRE( second->id == first->id );

    REQUIRE( firstFuture.wait_for( std::chrono::milliseconds( kWaitTimeoutMs ) )
             == std::future_status::ready );
    REQUIRE( secondFuture.wait_for( std::chrono::milliseconds( kWaitTimeoutMs ) )
             == std::future_status::ready );

    const auto firstInfo = firstFuture.get();
    const auto secondInfo = secondFuture.get();
    REQUIRE( firstInfo.has_value() );
    REQUIRE( secondInfo.has_value() );
    REQUIRE( firstInfo->id == first->id );
    REQUIRE( secondInfo->id == first->id );
    REQUIRE( secondInfo->total_lines == 3_lcount );

    const auto files = session->openFiles();
    REQUIRE( files.has_value() );
    REQUIRE( files->size() == 1 );

    REQUIRE( session->closeFile( first->id ).has_value() );
    REQUIRE( session->closeFile( first->id ).has_value() );
}

TEST_CASE( "API sync open joins an in-flight async open", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "one", "two", "three" } );

    const auto pending = session->openFileAsync( file.path.toStdString(), {} );
    REQUIRE( pending.has_value() );

    const auto joined = session->openFileSync( file.path.toStdString() );
    REQUIRE( joined.has_value() );
    REQUIRE( joined->id == pending->id );
    REQUIRE( joined->indexed );
    REQUIRE( joined->total_lines == 3_lcount );

    REQUIRE( session->closeFile( pending->id ).has_value() );
    REQUIRE( session->closeFile( pending->id ).has_value() );
}

TEST_CASE( "API fileInfo tracks the open file's line count", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "one", "two" } );

    const auto missing = session->fileInfo( 424242 );
    REQUIRE( !missing.has_value() );
    REQUIRE( missing.error().code == ErrorCode::FileNotOpen );

    const auto info = openSyncOrFail( *session, file.path );
    const auto fetched = session->fileInfo( info.id );
    REQUIRE( fetched.has_value() );
    REQUIRE( fetched->total_lines == 2_lcount );

    REQUIRE( session->closeFile( info.id ).has_value() );
    const auto closed = session->fileInfo( info.id );
    REQUIRE( !closed.has_value() );
    REQUIRE( closed.error().code == ErrorCode::FileNotOpen );
}

TEST_CASE( "API concurrent sync opens join the in-flight indexing", "[api][engine]" )
{
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    const auto file = writeLargeTempLogFile();
    const auto pending = session->openFileAsync( file.path.toStdString(), {} );
    REQUIRE( pending.has_value() );

    // Wait until the in-flight indexing is actually running, so both sync
    // opens below block in their nested waits instead of hitting a completed
    // entry
    REQUIRE( collector.waitFor( "indexing.progress",
                                "\"fileId\":" + std::to_string( pending->id ) ) );

    std::promise<Result<FileInfo>> firstDone;
    std::promise<Result<FileInfo>> secondDone;
    auto firstFuture = firstDone.get_future();
    auto secondFuture = secondDone.get_future();

    std::thread firstThread( [ & ] {
        firstDone.set_value( session->openFileSync( file.path.toStdString() ) );
    } );
    std::thread secondThread( [ & ] {
        secondDone.set_value( session->openFileSync( file.path.toStdString() ) );
    } );

    REQUIRE( firstFuture.wait_for( std::chrono::milliseconds( kWaitTimeoutMs ) )
             == std::future_status::ready );
    REQUIRE( secondFuture.wait_for( std::chrono::milliseconds( kWaitTimeoutMs ) )
             == std::future_status::ready );
    firstThread.join();
    secondThread.join();

    const auto firstInfo = firstFuture.get();
    const auto secondInfo = secondFuture.get();
    REQUIRE( firstInfo.has_value() );
    REQUIRE( secondInfo.has_value() );
    REQUIRE( firstInfo->id == pending->id );
    REQUIRE( secondInfo->id == pending->id );
    REQUIRE( firstInfo->indexed );
    REQUIRE( secondInfo->indexed );
    REQUIRE( firstInfo->total_lines == 1500000_lcount );
    REQUIRE( secondInfo->total_lines == 1500000_lcount );

    // One async plus two sync openers share one entry: three references
    const auto files = session->openFiles();
    REQUIRE( files.has_value() );
    REQUIRE( files->size() == 1 );

    REQUIRE( session->closeFile( pending->id ).has_value() );
    REQUIRE( session->closeFile( pending->id ).has_value() );
    REQUIRE( session->closeFile( pending->id ).has_value() );
    const auto afterClose = session->openFiles();
    REQUIRE( afterClose.has_value() );
    REQUIRE( afterClose->empty() );
}

TEST_CASE( "API open callback may call back into the session", "[api][engine]" )
{
    std::promise<Result<FileInfo>> openDone;
    std::promise<std::vector<LineData>> linesDone;
    auto openFuture = openDone.get_future();
    auto linesFuture = linesDone.get_future();

    auto session = createSession();
    const auto file = writeTempLogFile( { "one", "two", "three" } );

    // The callback fires on the engine thread; calling back into the session
    // from there must marshal onto the worker without deadlocking
    const auto opened = session->openFileAsync(
        file.path.toStdString(),
        [ &session, &openDone, &linesDone ]( Result<FileInfo> result ) {
            const auto id = result.has_value() ? result->id : klogg::api::FileId{ 0 };
            openDone.set_value( std::move( result ) );
            auto lines = session->getLines( id, 0_lnum, 3_lcount );
            linesDone.set_value( lines.has_value() ? *lines : std::vector<LineData>() );
        } );
    REQUIRE( opened.has_value() );

    REQUIRE( openFuture.wait_for( std::chrono::milliseconds( kWaitTimeoutMs ) )
             == std::future_status::ready );
    REQUIRE( linesFuture.wait_for( std::chrono::milliseconds( kWaitTimeoutMs ) )
             == std::future_status::ready );

    const auto openedInfo = openFuture.get();
    const auto lines = linesFuture.get();
    REQUIRE( openedInfo.has_value() );
    REQUIRE( openedInfo->id == opened->id );
    REQUIRE( lines.size() == 3 );
    REQUIRE( lines[ 0 ].text == "one" );
    REQUIRE( lines[ 2 ].text == "three" );

    REQUIRE( session->closeFile( opened->id ).has_value() );
}
