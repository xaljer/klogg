/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

#include <catch2/catch.hpp>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QThread>
#include <QUuid>

#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <vector>

#include "klogg/api.h"

using namespace klogg::api;

namespace {

constexpr int kWaitTimeoutMs = 20000;

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

TempLogFile writeTempLogFile( const QStringList& lines )
{
    TempLogFile file;
    file.path = QString( "%1/klogg_api_test_%2.log" )
                    .arg( QDir::tempPath(), QUuid::createUuid().toString( QUuid::WithoutBraces ) );

    QFile out( file.path );
    REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Text ) );
    QTextStream stream( &out );
    for ( const auto& line : lines ) {
        stream << line << "\n";
    }
    out.close();
    return file;
}

std::unique_ptr<Session> createSession()
{
    auto session = Session::create();
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

    bool waitFor( const std::string& type, const std::string& jsonSnippet = {},
                  int timeoutMs = kWaitTimeoutMs )
    {
        QElapsedTimer timer;
        timer.start();
        while ( timer.elapsed() < timeoutMs ) {
            {
                std::lock_guard lock( mutex );
                for ( const auto& [ eventType, json ] : events ) {
                    if ( eventType == type
                         && ( jsonSnippet.empty()
                              || json.find( jsonSnippet ) != std::string::npos ) ) {
                        return true;
                    }
                }
            }
            QThread::msleep( 10 );
        }
        return false;
    }
};

} // namespace

TEST_CASE( "API openFileSync indexes a file and reports its info", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "alpha", "beta beta", "gamma", "delta" } );

    const auto info = openSyncOrFail( *session, file.path );
    REQUIRE( info.indexed );
    REQUIRE( info.total_lines == 4_lcount );
    REQUIRE( info.size_bytes == QFileInfo{ file.path }.size() );
    REQUIRE( !info.encoding.name.empty() );
    REQUIRE( info.max_line_length >= 9_length );

    const auto fetched = session->fileInfo( info.id );
    REQUIRE( fetched.has_value() );
    REQUIRE( fetched->id == info.id );
    REQUIRE( fetched->path == info.path );
    REQUIRE( fetched->total_lines == 4_lcount );
}

TEST_CASE( "API openFileSync reports a missing file", "[api][engine]" )
{
    auto session = createSession();
    const auto missing = QDir::tempPath() + "/klogg_api_test_no_such_file.log";

    const auto info = session->openFileSync( missing.toStdString() );
    REQUIRE( !info.has_value() );
    REQUIRE( info.error().code == ErrorCode::FileNotFound );
}

TEST_CASE( "API openFileAsync delivers the result via callback", "[api][engine]" )
{
    // Declared before the session so it outlives callbacks fired on teardown
    std::promise<Result<FileInfo>> done;
    auto future = done.get_future();

    auto session = createSession();
    const auto file = writeTempLogFile( { "one", "two", "three" } );

    const auto pending
        = session->openFileAsync( file.path.toStdString(), [ &done ]( Result<FileInfo> result ) {
              done.set_value( std::move( result ) );
          } );
    REQUIRE( pending.has_value() );
    REQUIRE( !pending->indexed );
    REQUIRE( pending->path == file.path.toStdString() );

    REQUIRE( future.wait_for( std::chrono::milliseconds( kWaitTimeoutMs ) )
             == std::future_status::ready );
    const auto info = future.get();
    REQUIRE( info.has_value() );
    REQUIRE( info->indexed );
    REQUIRE( info->id == pending->id );
    REQUIRE( info->total_lines == 3_lcount );
}

TEST_CASE( "API getLines clamps out-of-range requests", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "l0", "l1", "l2", "l3", "l4" } );
    const auto info = openSyncOrFail( *session, file.path );

    const auto inRange = session->getLines( info.id, 1_lnum, 2_lcount );
    REQUIRE( inRange.has_value() );
    REQUIRE( inRange->size() == 2 );
    REQUIRE( inRange->at( 0 ).num == 1_lnum );
    REQUIRE( inRange->at( 0 ).text == "l1" );
    REQUIRE( inRange->at( 1 ).text == "l2" );

    const auto truncated = session->getLines( info.id, 3_lnum, 10_lcount );
    REQUIRE( truncated.has_value() );
    REQUIRE( truncated->size() == 2 );
    REQUIRE( truncated->at( 0 ).text == "l3" );
    REQUIRE( truncated->at( 1 ).text == "l4" );

    const auto beyond = session->getLines( info.id, 5_lnum, 1_lcount );
    REQUIRE( beyond.has_value() );
    REQUIRE( beyond->empty() );
}

TEST_CASE( "API getLines handles the three ANSI modes", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { QString::fromUtf8( "\x1b[31mred text\x1b[0m plain" ) } );
    const auto info = openSyncOrFail( *session, file.path );

    const auto raw = session->getLines( info.id, 0_lnum, 1_lcount, AnsiMode::Raw );
    REQUIRE( raw.has_value() );
    REQUIRE( raw->size() == 1 );
    REQUIRE( raw->at( 0 ).text.find( '\x1b' ) != std::string::npos );
    REQUIRE( raw->at( 0 ).segments.empty() );

    const auto stripped = session->getLines( info.id, 0_lnum, 1_lcount, AnsiMode::Strip );
    REQUIRE( stripped.has_value() );
    REQUIRE( stripped->at( 0 ).text.find( '\x1b' ) == std::string::npos );
    REQUIRE( stripped->at( 0 ).text == "red text plain" );
    REQUIRE( stripped->at( 0 ).segments.empty() );

    const auto segmented = session->getLines( info.id, 0_lnum, 1_lcount, AnsiMode::Segments );
    REQUIRE( segmented.has_value() );
    REQUIRE( segmented->at( 0 ).text == "red text plain" );
    REQUIRE( !segmented->at( 0 ).segments.empty() );
    REQUIRE( !segmented->at( 0 ).segments.front().fg.empty() );
}

TEST_CASE( "API validatePattern checks patterns", "[api][engine]" )
{
    auto session = createSession();

    const auto valid = session->validatePattern( "ERR.*", true );
    REQUIRE( valid.has_value() );
    REQUIRE( valid->valid );
    REQUIRE( !valid->isPlainText );

    const auto invalid = session->validatePattern( "([unclosed", true );
    REQUIRE( invalid.has_value() );
    REQUIRE( !invalid->valid );
    REQUIRE( !invalid->error.empty() );

    const auto plain = session->validatePattern( "hello", true );
    REQUIRE( plain.has_value() );
    REQUIRE( plain->valid );
    REQUIRE( plain->isPlainText );
}

TEST_CASE( "API runSearch finds matches in the whole file", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "INFO alpha", "WARN beta", "INFO gamma", "WARN delta" } );
    const auto info = openSyncOrFail( *session, file.path );

    SearchParams params;
    params.pattern = "WARN";
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );

    REQUIRE( waitForSearchDone( *session, *search ) );

    const auto progress = session->searchProgress( *search );
    REQUIRE( progress.has_value() );
    REQUIRE( progress->total_matches == 2_lcount );

    const auto matches = session->searchMatches( *search, 0, 10 );
    REQUIRE( matches.has_value() );
    REQUIRE( matches->size() == 2 );
    REQUIRE( matches->at( 0 ) == 1_lnum );
    REQUIRE( matches->at( 1 ) == 3_lnum );
}

TEST_CASE( "API runSearch honors invertMatch", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "INFO alpha", "WARN beta", "INFO gamma", "WARN delta" } );
    const auto info = openSyncOrFail( *session, file.path );

    SearchParams params;
    params.pattern = "WARN";
    params.invertMatch = true;
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );

    REQUIRE( waitForSearchDone( *session, *search ) );

    const auto matches = session->searchMatches( *search, 0, 10 );
    REQUIRE( matches.has_value() );
    REQUIRE( matches->size() == 2 );
    REQUIRE( matches->at( 0 ) == 0_lnum );
    REQUIRE( matches->at( 1 ) == 2_lnum );
}

TEST_CASE( "API runSearch with isBoolean honors case sensitivity", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "INFO alpha", "info beta", "WARN alpha" } );
    const auto info = openSyncOrFail( *session, file.path );

    SearchParams params;
    params.pattern = "\"INFO\" and \"alpha\"";
    params.isBoolean = true;
    params.caseSensitive = true;
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );
    REQUIRE( waitForSearchDone( *session, *search ) );

    const auto matches = session->searchMatches( *search, 0, 10 );
    REQUIRE( matches.has_value() );
    REQUIRE( matches->size() == 1 );
    REQUIRE( matches->at( 0 ) == 0_lnum );
}

TEST_CASE( "API runSearch honors the from/to line range", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "m0", "m1", "m2", "m3" } );
    const auto info = openSyncOrFail( *session, file.path );

    SearchParams params;
    params.pattern = "m";
    params.fromLine = 1_lnum;
    // toLine is exclusive: lines 1 and 2 are scanned
    params.toLine = 3_lnum;
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );
    REQUIRE( waitForSearchDone( *session, *search ) );

    const auto matches = session->searchMatches( *search, 0, 10 );
    REQUIRE( matches.has_value() );
    REQUIRE( matches->size() == 2 );
    REQUIRE( matches->at( 0 ) == 1_lnum );
    REQUIRE( matches->at( 1 ) == 2_lnum );
}

TEST_CASE( "API runSearch stops at maxResults", "[api][engine]" )
{
    // Declared before the session so it outlives events fired on teardown
    EventCollector collector;

    auto session = createSession();

    // The search must outlive the queued progress signal round-trip for the
    // maxResults interrupt to be observable: ~1.5M padded lines take ~1s
    // to scan (100k bare lines scan in ~10ms, faster than signal delivery)
    TempLogFile file;
    file.path = QString( "%1/klogg_api_test_%2.log" )
                    .arg( QDir::tempPath(), QUuid::createUuid().toString( QUuid::WithoutBraces ) );
    constexpr auto totalLines = 1500000;
    {
        QFile out( file.path );
        REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Text ) );
        QTextStream stream( &out );
        const QString padding( 100, QLatin1Char( 'x' ) );
        for ( auto i = 0; i < totalLines; ++i ) {
            stream << "match line " << i << " " << padding << "\n";
        }
    }
    const auto info = openSyncOrFail( *session, file.path );
    REQUIRE( info.total_lines == LinesCount( totalLines ) );

    session->subscribe( collector.handler() );

    SearchParams params;
    params.pattern = "match";
    params.maxResults = 1000;
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );

    REQUIRE( collector.waitFor( "search.completed", "\"searchId\":" + std::to_string( *search ) ) );

    const auto progress = session->searchProgress( *search );
    REQUIRE( progress.has_value() );
    REQUIRE( progress->total_matches >= 1000_lcount );
    REQUIRE( progress->total_matches < LinesCount( totalLines ) );
}

TEST_CASE( "API runBooleanSearch evaluates boolean expressions", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "INFO alpha", "INFO beta", "WARN alpha" } );
    const auto info = openSyncOrFail( *session, file.path );

    const auto search = session->runBooleanSearch( info.id, "\"INFO\" and \"alpha\"" );
    REQUIRE( search.has_value() );
    REQUIRE( waitForSearchDone( *session, *search ) );

    const auto matches = session->searchMatches( *search, 0, 10 );
    REQUIRE( matches.has_value() );
    REQUIRE( matches->size() == 1 );
    REQUIRE( matches->at( 0 ) == 0_lnum );
}

TEST_CASE( "API cancelSearch is idempotent and blocks match access", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "alpha", "beta", "gamma" } );
    const auto info = openSyncOrFail( *session, file.path );

    SearchParams params;
    params.pattern = "a";
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );

    const auto cancelled = session->cancelSearch( *search );
    REQUIRE( cancelled.has_value() );

    const auto matches = session->searchMatches( *search, 0, 10 );
    REQUIRE( !matches.has_value() );
    REQUIRE( matches.error().code == ErrorCode::SearchCancelled );

    // Repeated cancel and cancel of an unknown id both succeed
    REQUIRE( session->cancelSearch( *search ).has_value() );
    REQUIRE( session->cancelSearch( 424242 ).has_value() );
}

TEST_CASE( "API closeFile invalidates the file id", "[api][engine]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "alpha", "beta" } );
    const auto info = openSyncOrFail( *session, file.path );

    REQUIRE( session->closeFile( info.id ).has_value() );

    const auto lines = session->getLines( info.id, 0_lnum, 1_lcount );
    REQUIRE( !lines.has_value() );
    REQUIRE( lines.error().code == ErrorCode::FileNotOpen );

    const auto closedAgain = session->closeFile( info.id );
    REQUIRE( !closedAgain.has_value() );
    REQUIRE( closedAgain.error().code == ErrorCode::InvalidFileId );
}

TEST_CASE( "API subscribe delivers file and search events", "[api][engine]" )
{
    // Declared before the session so it outlives events fired on teardown
    EventCollector collector;

    auto session = createSession();
    session->subscribe( collector.handler() );

    const auto file = writeTempLogFile( { "alpha", "beta" } );
    const auto info = openSyncOrFail( *session, file.path );
    REQUIRE( collector.waitFor( "file.indexed", "\"fileId\":" + std::to_string( info.id ) ) );

    SearchParams params;
    params.pattern = "alpha";
    const auto search = session->runSearch( info.id, params );
    REQUIRE( search.has_value() );
    REQUIRE( collector.waitFor( "search.completed", "\"searchId\":" + std::to_string( *search ) ) );
}

TEST_CASE( "API openFiles tracks open and closed files", "[api][engine]" )
{
    auto session = createSession();
    const auto first = writeTempLogFile( { "one" } );
    const auto second = writeTempLogFile( { "two" } );

    const auto firstInfo = openSyncOrFail( *session, first.path );
    const auto secondInfo = openSyncOrFail( *session, second.path );

    auto files = session->openFiles();
    REQUIRE( files.has_value() );
    REQUIRE( files->size() == 2 );

    REQUIRE( session->closeFile( firstInfo.id ).has_value() );
    files = session->openFiles();
    REQUIRE( files.has_value() );
    REQUIRE( files->size() == 1 );
    REQUIRE( files->at( 0 ).id == secondInfo.id );
}

TEST_CASE( "API version is not empty", "[api][engine]" )
{
    auto session = createSession();
    REQUIRE( !session->version().empty() );
}
