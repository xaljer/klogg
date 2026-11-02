/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

#include <catch2/catch.hpp>

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QUuid>

#include <memory>
#include <mutex>
#include <optional>
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
    file.path = QString( "%1/klogg_api_events_test_%2.log" )
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

    bool contains( const std::string& type )
    {
        std::lock_guard lock( mutex );
        for ( const auto& [ eventType, json ] : events ) {
            if ( eventType == type ) {
                return true;
            }
        }
        return false;
    }
};

bool waitForExtractionDone( Session& session, ExtractionId id, int timeoutMs = kWaitTimeoutMs )
{
    QElapsedTimer timer;
    timer.start();
    while ( timer.elapsed() < timeoutMs ) {
        const auto progress = session.extractionProgress( id );
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

int64_t expectedTimestampNs( const QString& text )
{
    return QDateTime::fromString( text, QStringLiteral( "yyyy-MM-dd HH:mm:ss.zzz" ) )
               .toMSecsSinceEpoch()
           * 1000000LL;
}

LogTypeConfig makeTestLogType()
{
    LogTypeConfig config{};
    config.name = "Test App Log";
    config.globPattern = "*.testlog";
    config.parsingRule.name = "test-parser";
    config.parsingRule.globPattern = "*.testlog";
    config.parsingRule.parsePattern
        = R"(^(?<timestamp>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) (?<log_level>\w+))";
    config.parsingRule.timestampFormat = "yyyy-MM-dd HH:mm:ss.zzz";

    EventRule request;
    request.name = "request";
    request.category = "http";
    request.description = "request start";
    request.eventType = EventType::Instance;
    request.pattern = R"(Request started id=(?<reqid>\d+))";
    request.parameterNames = { "reqid" };

    EventRule transaction;
    transaction.name = "transaction";
    transaction.category = "db";
    transaction.eventType = EventType::Slice;
    transaction.beginPattern = R"(Transaction tx=(?<tx>\d+) begin)";
    transaction.endPattern = R"(Transaction tx=(?<tx>\d+) end status=(?<status>\w+))";
    transaction.asyncIdParam = "tx";
    transaction.parameterNames = { "tx" };
    transaction.endParameterNames = { "status" };

    EventRule task;
    task.name = "task";
    task.category = "compute";
    task.eventType = EventType::Slice;
    task.beginPattern = R"(Task finished took=(?<dur>\d+)ms)";
    task.durationParam = "dur";
    task.durationUnit = DurationUnit::Ms;
    task.parameterNames = { "dur" };

    config.eventRulesByCategory[ "http" ] = { request };
    config.eventRulesByCategory[ "db" ] = { transaction };
    config.eventRulesByCategory[ "compute" ] = { task };
    return config;
}

QStringList testLogLines()
{
    return {
        "2024-01-15 10:00:00.000 INFO Request started id=42",
        "2024-01-15 10:00:00.050 INFO Transaction tx=7 begin",
        "2024-01-15 10:00:00.100 INFO Request started id=43",
        "2024-01-15 10:00:00.150 INFO Transaction tx=7 end status=ok",
        "2024-01-15 10:00:00.200 INFO Task finished took=30ms",
        "2024-01-15 10:00:00.250 INFO Transaction tx=9 begin",
        "plain line without timestamp Request started id=44",
    };
}

struct ExtractionFixture {
    std::unique_ptr<Session> session;
    TempLogFile file;
    FileId fileId = 0;
    std::string configName;
    EventCollector collector;

    ExtractionFixture()
    {
        session = createSession();
        file = writeTempLogFile( testLogLines() );
        fileId = openSyncOrFail( *session, file.path ).id;
        const std::vector<LogTypeConfig> configs{ makeTestLogType() };
        REQUIRE( session->replaceLogTypeConfigs( configs ).has_value() );
        configName = configs.front().name;
        session->subscribe( collector.handler() );
    }

    ExtractionId runExtraction()
    {
        ExtractionParams params;
        params.logTypeConfigName = configName;
        const auto extraction = session->extractEvents( fileId, params );
        REQUIRE( extraction.has_value() );
        REQUIRE( waitForExtractionDone( *session, *extraction ) );
        return *extraction;
    }
};

} // namespace

TEST_CASE( "log type configs replace and list", "[api][events]" )
{
    auto session = createSession();

    const auto initial = session->listLogTypeConfigs();
    REQUIRE( initial.has_value() );
    REQUIRE( initial->empty() );

    REQUIRE( session->replaceLogTypeConfigs( { makeTestLogType() } ).has_value() );

    auto configs = session->listLogTypeConfigs();
    REQUIRE( configs.has_value() );
    REQUIRE( configs->size() == 1 );

    const auto& listed = ( *configs )[ 0 ];
    REQUIRE( listed.name == "Test App Log" );
    REQUIRE( listed.globPattern == "*.testlog" );
    REQUIRE( listed.enabled );
    REQUIRE( !listed.isDefault );
    REQUIRE( listed.parsingRule.name == "test-parser" );
    REQUIRE( listed.parsingRule.globPattern == "*.testlog" );
    REQUIRE( listed.parsingRule.timestampFormat == "yyyy-MM-dd HH:mm:ss.zzz" );
    REQUIRE( listed.eventRulesByCategory.size() == 3 );
    REQUIRE( listed.eventRulesByCategory.at( "http" ).size() == 1 );
    REQUIRE( listed.eventRulesByCategory.at( "http" )[ 0 ].name == "request" );
    REQUIRE( listed.eventRulesByCategory.at( "http" )[ 0 ].parameterNames
             == std::vector<std::string>{ "reqid" } );
    REQUIRE( listed.eventRulesByCategory.at( "db" )[ 0 ].asyncIdParam == "tx" );
    REQUIRE( listed.eventRulesByCategory.at( "compute" )[ 0 ].durationParam == "dur" );
    REQUIRE( listed.eventRulesByCategory.at( "compute" )[ 0 ].durationUnit == DurationUnit::Ms );

    auto updated = makeTestLogType();
    updated.name = "Renamed Log";
    updated.enabled = false;
    REQUIRE( session->replaceLogTypeConfigs( { updated } ).has_value() );
    configs = session->listLogTypeConfigs();
    REQUIRE( configs->size() == 1 );
    REQUIRE( ( *configs )[ 0 ].name == "Renamed Log" );
    REQUIRE( !( *configs )[ 0 ].enabled );

    // An empty list clears the in-memory configuration
    REQUIRE( session->replaceLogTypeConfigs( {} ).has_value() );
    configs = session->listLogTypeConfigs();
    REQUIRE( configs->empty() );
}

TEST_CASE( "log type configs are in-memory only", "[api][events]" )
{
    // The core never reads or writes log_visualization.json: a session created
    // after a replacement starts empty (the shell owns persistence)
    {
        auto session = createSession();
        REQUIRE( session->replaceLogTypeConfigs( { makeTestLogType() } ).has_value() );
        REQUIRE( session->listLogTypeConfigs()->size() == 1 );
    }

    auto session = createSession();
    const auto configs = session->listLogTypeConfigs();
    REQUIRE( configs.has_value() );
    REQUIRE( configs->empty() );
}

TEST_CASE( "extractEvents extracts instance and slice events", "[api][events]" )
{
    ExtractionFixture fixture;
    const auto extractionId = fixture.runExtraction();

    const auto progress = fixture.session->extractionProgress( extractionId );
    REQUIRE( progress.has_value() );
    REQUIRE( progress->progress == 1.0f );
    REQUIRE( progress->eventsFound == 7 );

    REQUIRE( fixture.collector.contains( "event.extraction.progress" ) );
    REQUIRE( fixture.collector.waitFor( "event.extraction.completed",
                                        "\"extractionId\":" + std::to_string( extractionId ) ) );

    const auto events = fixture.session->extractedEvents( extractionId, 0, 100 );
    REQUIRE( events.has_value() );
    REQUIRE( events->size() == 7 );

    const auto& request = ( *events )[ 0 ];
    REQUIRE( request.name == "request" );
    REQUIRE( request.category == "http" );
    REQUIRE( request.eventType == EventType::Instance );
    REQUIRE( request.sliceType == SliceType::None );
    REQUIRE( request.line == 0_lnum );
    REQUIRE( request.hasValidTimestamp );
    REQUIRE( request.timestampNs == expectedTimestampNs( "2024-01-15 10:00:00.000" ) );
    REQUIRE( request.logLevel == "INFO" );
    REQUIRE( request.parameters.at( "reqid" ) == "42" );

    const auto& begin = ( *events )[ 1 ];
    REQUIRE( begin.name == "transaction" );
    REQUIRE( begin.eventType == EventType::Slice );
    REQUIRE( begin.sliceType == SliceType::Begin );
    REQUIRE( begin.traceId == "7" );
    REQUIRE( begin.parameters.at( "tx" ) == "7" );

    const auto& end = ( *events )[ 3 ];
    REQUIRE( end.sliceType == SliceType::End );
    REQUIRE( end.traceId == "7" );
    REQUIRE( end.parameters.at( "status" ) == "ok" );

    const auto& task = ( *events )[ 4 ];
    REQUIRE( task.name == "task" );
    REQUIRE( task.eventType == EventType::Slice );
    REQUIRE( task.sliceType == SliceType::None );
    REQUIRE( task.durationNs == 30 * 1000000LL );
    REQUIRE( task.timestampNs == expectedTimestampNs( "2024-01-15 10:00:00.200" ) );
    REQUIRE( task.startTimestampNs == task.timestampNs - task.durationNs );

    const auto& noTimestamp = ( *events )[ 6 ];
    REQUIRE( noTimestamp.name == "request" );
    REQUIRE( noTimestamp.parameters.at( "reqid" ) == "44" );
    REQUIRE( !noTimestamp.hasValidTimestamp );

    // Pagination boundaries
    REQUIRE( fixture.session->extractedEvents( extractionId, 0, 3 )->size() == 3 );
    REQUIRE( fixture.session->extractedEvents( extractionId, 3, 3 )->size() == 3 );
    REQUIRE( fixture.session->extractedEvents( extractionId, 6, 10 )->size() == 1 );
    REQUIRE( fixture.session->extractedEvents( extractionId, 7, 1 )->empty() );
    REQUIRE( fixture.session->extractedEvents( extractionId, 0, 0 )->empty() );
}

TEST_CASE( "sliceRanges pairs begin/end and reports orphans", "[api][events]" )
{
    ExtractionFixture fixture;
    const auto extractionId = fixture.runExtraction();

    const auto ranges = fixture.session->sliceRanges( extractionId );
    REQUIRE( ranges.has_value() );
    REQUIRE( ranges->size() == 3 );

    const auto& paired = ( *ranges )[ 0 ];
    REQUIRE( paired.name == "transaction" );
    REQUIRE( paired.category == "db" );
    REQUIRE( paired.traceId == "7" );
    REQUIRE( paired.beginLine == 1_lnum );
    REQUIRE( paired.endLine == 3_lnum );

    const auto& complete = ( *ranges )[ 1 ];
    REQUIRE( complete.name == "task" );
    REQUIRE( complete.beginLine == 4_lnum );
    REQUIRE( complete.endLine == 4_lnum );

    // Orphan begin extends to the last line of the scanned range
    const auto& orphan = ( *ranges )[ 2 ];
    REQUIRE( orphan.name == "transaction" );
    REQUIRE( orphan.traceId == "9" );
    REQUIRE( orphan.beginLine == 5_lnum );
    REQUIRE( orphan.endLine == 6_lnum );
}

TEST_CASE( "extractEvents honors the line range", "[api][events]" )
{
    ExtractionFixture fixture;

    ExtractionParams params;
    params.logTypeConfigName = fixture.configName;
    params.fromLine = 2_lnum;
    params.toLine = LineNumber( 5 );
    const auto extraction = fixture.session->extractEvents( fixture.fileId, params );
    REQUIRE( extraction.has_value() );
    REQUIRE( waitForExtractionDone( *fixture.session, *extraction ) );

    const auto events = fixture.session->extractedEvents( *extraction, 0, 100 );
    REQUIRE( events->size() == 3 );
    REQUIRE( ( *events )[ 0 ].line == 2_lnum );
    REQUIRE( ( *events )[ 1 ].line == 3_lnum );
    REQUIRE( ( *events )[ 2 ].line == 4_lnum );

    // The begin of tx=7 is outside the range: its end pairs with nothing
    const auto ranges = fixture.session->sliceRanges( *extraction );
    REQUIRE( ranges->size() == 1 );
    REQUIRE( ( *ranges )[ 0 ].name == "task" );
}

TEST_CASE( "exportEvents writes chrome trace json", "[api][events]" )
{
    ExtractionFixture fixture;
    const auto extractionId = fixture.runExtraction();

    const auto outputPath
        = QString( "%1/klogg_api_events_export_%2.json" )
              .arg( QDir::tempPath(), QUuid::createUuid().toString( QUuid::WithoutBraces ) )
              .toStdString();

    const auto exported
        = fixture.session->exportEvents( extractionId, outputPath, ExportFormat::Json );
    REQUIRE( exported.has_value() );
    REQUIRE( exported->outputPath == outputPath );
    REQUIRE( exported->eventCount == 5 );
    REQUIRE( exported->skippedCount == 1 );
    REQUIRE( exported->completeEventCount == 2 );
    REQUIRE( exported->orphanBeginCount == 1 );
    REQUIRE( exported->orphanEndCount == 0 );

    QFile outFile{ QString::fromStdString( outputPath ) };
    REQUIRE( outFile.open( QIODevice::ReadOnly ) );
    const auto document = QJsonDocument::fromJson( outFile.readAll() );
    outFile.close();
    QFile::remove( QString::fromStdString( outputPath ) );

    REQUIRE( document.isObject() );
    const auto traceEvents = document.object()[ QStringLiteral( "traceEvents" ) ].toArray();
    REQUIRE( traceEvents.size() == 5 );

    int instantCount = 0;
    int completeCount = 0;
    int beginCount = 0;
    for ( const auto& value : traceEvents ) {
        const auto event = value.toObject();
        const auto phase = event[ QStringLiteral( "ph" ) ].toString();
        if ( phase == QLatin1String( "i" ) ) {
            ++instantCount;
            REQUIRE( event[ QStringLiteral( "name" ) ].toString() == QLatin1String( "request" ) );
        }
        else if ( phase == QLatin1String( "X" ) ) {
            ++completeCount;
            if ( event[ QStringLiteral( "name" ) ].toString() == QLatin1String( "task" ) ) {
                // Chrome trace JSON carries us; the native duration is ns
                REQUIRE( event[ QStringLiteral( "dur" ) ].toInteger() == 30000 );
                REQUIRE( event[ QStringLiteral( "ts" ) ].toInteger()
                         == expectedTimestampNs( "2024-01-15 10:00:00.200" ) / 1000 - 30000 );
            }
        }
        else if ( phase == QLatin1String( "B" ) ) {
            ++beginCount;
            REQUIRE( event[ QStringLiteral( "id" ) ].toString() == QLatin1String( "9" ) );
        }
    }
    REQUIRE( instantCount == 2 );
    REQUIRE( completeCount == 2 );
    REQUIRE( beginCount == 1 );

    const auto binary
        = fixture.session->exportEvents( extractionId, outputPath, ExportFormat::BinaryProto );
    REQUIRE( !binary.has_value() );
    REQUIRE( binary.error().code == ErrorCode::InternalError );
}

TEST_CASE( "exportEvents filters by categories", "[api][events]" )
{
    ExtractionFixture fixture;
    const auto extractionId = fixture.runExtraction();

    const auto outputPath
        = QString( "%1/klogg_api_events_export_%2.json" )
              .arg( QDir::tempPath(), QUuid::createUuid().toString( QUuid::WithoutBraces ) )
              .toStdString();

    const auto readCategories = [ &outputPath ]() {
        QFile outFile{ QString::fromStdString( outputPath ) };
        REQUIRE( outFile.open( QIODevice::ReadOnly ) );
        const auto document = QJsonDocument::fromJson( outFile.readAll() );
        outFile.close();
        QFile::remove( QString::fromStdString( outputPath ) );

        REQUIRE( document.isObject() );
        const auto traceEvents = document.object()[ QStringLiteral( "traceEvents" ) ].toArray();
        QStringList categories;
        for ( const auto& value : traceEvents ) {
            categories.append( value.toObject()[ QStringLiteral( "cat" ) ].toString() );
        }
        return categories;
    };

    // Subset: "http" only (2 valid instants + 1 skipped without timestamp)
    const auto httpOnly = fixture.session->exportEvents(
        extractionId, outputPath, ExportFormat::Json, std::vector<std::string>{ "http" } );
    REQUIRE( httpOnly.has_value() );
    REQUIRE( httpOnly->eventCount == 2 );
    REQUIRE( httpOnly->skippedCount == 1 );
    REQUIRE( httpOnly->completeEventCount == 0 );
    REQUIRE( httpOnly->orphanBeginCount == 0 );
    REQUIRE( httpOnly->orphanEndCount == 0 );

    const auto httpCategories = readCategories();
    REQUIRE( httpCategories.size() == 2 );
    for ( const auto& category : httpCategories ) {
        REQUIRE( category == QLatin1String( "http" ) );
    }

    // Subset: "db" + "compute" (paired slice, orphan begin, complete slice)
    const auto slices = fixture.session->exportEvents(
        extractionId, outputPath, ExportFormat::Json, std::vector<std::string>{ "db", "compute" } );
    REQUIRE( slices.has_value() );
    REQUIRE( slices->eventCount == 3 );
    REQUIRE( slices->skippedCount == 0 );
    REQUIRE( slices->completeEventCount == 2 );
    REQUIRE( slices->orphanBeginCount == 1 );
    REQUIRE( slices->orphanEndCount == 0 );

    const auto sliceCategories = readCategories();
    REQUIRE( sliceCategories.size() == 3 );
    for ( const auto& category : sliceCategories ) {
        REQUIRE( category != QLatin1String( "http" ) );
    }

    // nullopt exports everything, same as the default argument
    const auto all = fixture.session->exportEvents( extractionId, outputPath, ExportFormat::Json,
                                                    std::nullopt );
    REQUIRE( all.has_value() );
    REQUIRE( all->eventCount == 5 );
    REQUIRE( all->skippedCount == 1 );
    REQUIRE( readCategories().size() == 5 );

    // An empty vector matches no category
    const auto none = fixture.session->exportEvents( extractionId, outputPath, ExportFormat::Json,
                                                     std::vector<std::string>{} );
    REQUIRE( !none.has_value() );
    REQUIRE( none.error().code == ErrorCode::InternalError );
}

TEST_CASE( "generateEventPattern builds a named-group regex", "[api][events]" )
{
    auto session = createSession();

    const auto pattern = session->generateEventPattern( "Request {42} handled" );
    REQUIRE( pattern.has_value() );
    REQUIRE( pattern->find( "(?<Request>" ) != std::string::npos );

    const QRegularExpression regex{ QString::fromStdString( *pattern ) };
    REQUIRE( regex.isValid() );
    REQUIRE( regex.match( QStringLiteral( "Request {42} handled" ) ).hasMatch() );
}

TEST_CASE( "cancelExtraction invalidates the handle", "[api][events]" )
{
    ExtractionFixture fixture;

    ExtractionParams params;
    params.logTypeConfigName = fixture.configName;
    const auto extraction = fixture.session->extractEvents( fixture.fileId, params );
    REQUIRE( extraction.has_value() );

    REQUIRE( fixture.session->cancelExtraction( *extraction ).has_value() );

    const auto progress = fixture.session->extractionProgress( *extraction );
    REQUIRE( !progress.has_value() );
    REQUIRE( progress.error().code == ErrorCode::ExtractionCancelled );

    // Cancel is idempotent, unknown ids are a no-op
    REQUIRE( fixture.session->cancelExtraction( *extraction ).has_value() );
    REQUIRE( fixture.session->cancelExtraction( 424242 ).has_value() );
}

TEST_CASE( "lineFlags event bit mirrors the latest completed extraction", "[api][events]" )
{
    ExtractionFixture fixture;
    const auto extractionId = fixture.runExtraction();

    // Every line of the test log holds an event
    const auto flags = fixture.session->lineFlags( fixture.fileId, std::nullopt, 0_lnum, 7_lcount );
    REQUIRE( flags.has_value() );
    REQUIRE( flags->size() == 7 );
    for ( const auto flagBits : *flags ) {
        REQUIRE( ( flagBits & LineFlagEvent ) != 0 );
    }

    // Cancelling the sourcing extraction drops the bit
    REQUIRE( fixture.session->cancelExtraction( extractionId ).has_value() );
    const auto afterCancel
        = fixture.session->lineFlags( fixture.fileId, std::nullopt, 0_lnum, 7_lcount );
    REQUIRE( afterCancel.has_value() );
    for ( const auto flagBits : *afterCancel ) {
        REQUIRE( ( flagBits & LineFlagEvent ) == 0 );
    }
}

TEST_CASE( "lineFlags event bit is dropped by a reload", "[api][events]" )
{
    ExtractionFixture fixture;
    fixture.runExtraction();

    const auto flags = fixture.session->lineFlags( fixture.fileId, std::nullopt, 0_lnum, 7_lcount );
    REQUIRE( flags.has_value() );
    REQUIRE( ( flags->front() & LineFlagEvent ) != 0 );

    const auto reloaded = fixture.session->reloadFile( fixture.fileId );
    REQUIRE( reloaded.has_value() );

    const auto afterReload
        = fixture.session->lineFlags( fixture.fileId, std::nullopt, 0_lnum, 7_lcount );
    REQUIRE( afterReload.has_value() );
    for ( const auto flagBits : *afterReload ) {
        REQUIRE( ( flagBits & LineFlagEvent ) == 0 );
    }
}

TEST_CASE( "completed extraction joins the search union under events visibility", "[api][events]" )
{
    auto session = createSession();
    // Event lines {0, 2, 4, 5} interleaved with plain ones {1, 3, 6}
    const auto file = writeTempLogFile( {
        "2024-01-15 10:00:00.000 INFO Request started id=42",
        "2024-01-15 10:00:00.050 INFO plain note one",
        "2024-01-15 10:00:00.100 INFO Transaction tx=7 begin",
        "2024-01-15 10:00:00.150 INFO plain note two",
        "2024-01-15 10:00:00.200 INFO Transaction tx=7 end status=ok",
        "2024-01-15 10:00:00.250 INFO Task finished took=30ms",
        "2024-01-15 10:00:00.300 INFO plain note three",
    } );
    const auto info = openSyncOrFail( *session, file.path );
    const std::vector<LogTypeConfig> configs{ makeTestLogType() };
    REQUIRE( session->replaceLogTypeConfigs( configs ).has_value() );

    SearchParams searchParams;
    searchParams.pattern = "plain note";
    const auto search = session->runSearch( info.id, searchParams );
    REQUIRE( search.has_value() );
    REQUIRE( waitForSearchDone( *session, *search ) );

    ExtractionParams extractionParams;
    extractionParams.logTypeConfigName = configs.front().name;
    const auto extraction = session->extractEvents( info.id, extractionParams );
    REQUIRE( extraction.has_value() );
    REQUIRE( waitForExtractionDone( *session, *extraction ) );

    // Matches {1,3,6} union events {0,2,4,5} = the whole file
    const auto unionCount = session->setSearchVisibility( *search, true, false, true );
    REQUIRE( unionCount.has_value() );
    REQUIRE( *unionCount == 7_lcount );

    const auto projection = session->getFilteredLines( *search, 0, 10 );
    REQUIRE( projection.has_value() );
    REQUIRE( projection->size() == 7 );
    for ( size_t i = 0; i < projection->size(); ++i ) {
        REQUIRE( projection->at( i ).num == LineNumber( i ) );
    }

    const auto flags = session->lineFlags( info.id, *search, 0_lnum, 7_lcount );
    REQUIRE( flags.has_value() );
    REQUIRE( flags->at( 0 ) == LineFlagEvent );
    REQUIRE( flags->at( 1 ) == LineFlagMatch );
    REQUIRE( flags->at( 2 ) == LineFlagEvent );
    REQUIRE( flags->at( 3 ) == LineFlagMatch );
    REQUIRE( flags->at( 4 ) == LineFlagEvent );
    REQUIRE( flags->at( 5 ) == LineFlagEvent );
    REQUIRE( flags->at( 6 ) == LineFlagMatch );

    // The category filter narrows the projection but not the gutter bits
    REQUIRE( session->setEventCategoryFilter( info.id, { "db" } ).has_value() );
    const auto filteredCount = session->getFilteredLineCount( *search );
    REQUIRE( filteredCount.has_value() );
    REQUIRE( *filteredCount == 5_lcount );

    const auto narrowed = session->getFilteredLines( *search, 0, 10 );
    REQUIRE( narrowed.has_value() );
    REQUIRE( narrowed->size() == 5 );
    const std::vector<uint64_t> expectedLines{ 1, 2, 3, 4, 6 };
    for ( size_t i = 0; i < expectedLines.size(); ++i ) {
        REQUIRE( narrowed->at( i ).num == LineNumber( expectedLines[ i ] ) );
    }

    const auto flagsAfter = session->lineFlags( info.id, *search, 0_lnum, 7_lcount );
    REQUIRE( flagsAfter.has_value() );
    REQUIRE( *flagsAfter == *flags );

    // Empty filter restores all categories
    REQUIRE( session->setEventCategoryFilter( info.id, {} ).has_value() );
    const auto restoredCount = session->getFilteredLineCount( *search );
    REQUIRE( restoredCount.has_value() );
    REQUIRE( *restoredCount == 7_lcount );

    const auto badFile = session->setEventCategoryFilter( 424242, {} );
    REQUIRE( !badFile.has_value() );
    REQUIRE( badFile.error().code == ErrorCode::FileNotOpen );
}

TEST_CASE( "event api reports invalid ids", "[api][events]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "alpha" } );
    const auto info = openSyncOrFail( *session, file.path );

    const auto progress = session->extractionProgress( 424242 );
    REQUIRE( !progress.has_value() );
    REQUIRE( progress.error().code == ErrorCode::InvalidExtractionId );

    const auto events = session->extractedEvents( 424242, 0, 10 );
    REQUIRE( !events.has_value() );
    REQUIRE( events.error().code == ErrorCode::InvalidExtractionId );

    const auto ranges = session->sliceRanges( 424242 );
    REQUIRE( !ranges.has_value() );
    REQUIRE( ranges.error().code == ErrorCode::InvalidExtractionId );

    const auto exported = session->exportEvents( 424242, "out.json", ExportFormat::Json );
    REQUIRE( !exported.has_value() );
    REQUIRE( exported.error().code == ErrorCode::InvalidExtractionId );

    ExtractionParams params;
    params.logTypeConfigName = "no-such-config";
    const auto badConfig = session->extractEvents( info.id, params );
    REQUIRE( !badConfig.has_value() );
    REQUIRE( badConfig.error().code == ErrorCode::InternalError );

    params.logTypeConfigName.clear();
    const auto badFile = session->extractEvents( 424242, params );
    REQUIRE( !badFile.has_value() );
    REQUIRE( badFile.error().code == ErrorCode::FileNotOpen );
}
