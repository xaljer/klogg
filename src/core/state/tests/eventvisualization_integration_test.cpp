/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

#include <catch2/catch.hpp>

#include "eventvisualization.h"
#include "log.h"
#include "log_qt.h"
#include "logdata.h"
#include "regularexpression.h"

#include "eventvisualization_test_common.h"

#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <QTextStream>

#include <chrono>
#include <iostream>

SCENARIO( "Integration: extract events from real log file", "[event][integration]" )
{
    QString testLogFile = getTestLogFile();

    if ( testLogFile.isEmpty() ) {
        INFO( "Skipping integration test - set KLOGG_TEST_LOG_FILE environment variable" );
        return;
    }

    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    GIVEN( "A real log file and event rules" )
    {
        REQUIRE( QFile::exists( testLogFile ) );
        std::cout << "Using test log file: " << testLogFile.toStdString() << std::endl;

        manager.setEventRules( {} );

        EventRule indexRule;
        indexRule.name = "IndexOperation";
        indexRule.category = "Indexing";
        indexRule.eventType = EventType::Instance;
        indexRule.pattern = R"(\[IndexOperation::\w+@\d+\])";
        indexRule.enabled = true;
        manager.addEventRule( indexRule );

        EventRule searchRule;
        searchRule.name = "SearchOperation";
        searchRule.category = "Search";
        searchRule.eventType = EventType::Instance;
        searchRule.pattern = R"(\[SearchOperation::\w+@\d+\])";
        searchRule.enabled = true;
        manager.addEventRule( searchRule );

        WHEN( "Extracting events from file" )
        {
            auto start = std::chrono::high_resolution_clock::now();
            auto events
                = manager.extractEventsFromFile( testLogFile.toStdString(), manager.getEventRules() );
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( end - start );

            THEN( "Events are extracted successfully" )
            {
                REQUIRE( !events.empty() );
                std::cout << "Extracted " << events.size() << " events in " << duration.count()
                          << " ms" << std::endl;
            }

            AND_WHEN( "Converting to Perfetto format" )
            {
                auto perfettoEvents = manager.convertToPerfettoFormat( events );

                THEN( "Perfetto events are generated" )
                {
                    REQUIRE( !perfettoEvents.empty() );
                    std::cout << "Generated " << perfettoEvents.size() << " Perfetto events"
                              << std::endl;
                }
            }
        }
    }
}

SCENARIO( "Integration: klogg RegularExpression with real log file", "[event][integration]" )
{
    QString testLogFile = getTestLogFile();

    if ( testLogFile.isEmpty() ) {
        INFO( "Skipping integration test - set KLOGG_TEST_LOG_FILE environment variable" );
        return;
    }

    GIVEN( "A real log file" )
    {
        QFile file( testLogFile );
        REQUIRE( file.open( QIODevice::ReadOnly | QIODevice::Text ) );

        QStringList lines;
        while ( !file.atEnd() ) {
            lines.append( QString::fromUtf8( file.readLine() ).trimmed() );
        }
        file.close();

        std::cout << "Loaded " << lines.size() << " lines from test log file" << std::endl;

        WHEN( "Searching with klogg RegularExpression" )
        {
            RegularExpressionPattern pattern( "IndexOperation", false, false, false, false );
            RegularExpression expression( pattern );
            const auto matcher = expression.createMatcher();

            auto start = std::chrono::high_resolution_clock::now();
            int matchCount = 0;
            for ( const QString& line : lines ) {
                if ( matcher->hasMatch( line.toStdString() ) ) {
                    matchCount++;
                }
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( end - start );

            THEN( "Search completes successfully" )
            {
                std::cout << "Found " << matchCount << " matches in " << duration.count()
                          << " ms" << std::endl;
                REQUIRE( duration.count() >= 0 );
            }
        }
    }
}

SCENARIO( "Integration: Load config and export events", "[event][integration]" )
{
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    GIVEN( "A valid configuration file with parsing rules and event rules" )
    {
        QString configContent = "{\n"
                                "  \"logTypes\": [\n"
                                "    {\n"
                                "      \"name\": \"Test Log\",\n"
                                "      \"globPattern\": \"*\",\n"
                                "      \"enabled\": true,\n"
                                "      \"isDefault\": true,\n"
                                "      \"parsingRule\": {\n"
                                "        \"name\": \"Test Format\",\n"
                                "        \"parsePattern\": \"^(?<timestamp>\\\\d{4}-\\\\d{2}-\\\\d{2}T\\\\d{2}:\\\\d{2}:\\\\d{2}\\\\.\\\\d{3})\\\\s+(?<level>\\\\w+)\\\\s+(?<message>.*)$\",\n"
                                "        \"timestampFormat\": \"yyyy-MM-ddTHH:mm:ss.zzz\",\n"
                                "        \"enabled\": true\n"
                                "      },\n"
                                "      \"eventRules\": {\n"
                                "        \"TestCategory\": [\n"
                                "          {\n"
                                "            \"name\": \"test_event\",\n"
                                "            \"description\": \"A test event\",\n"
                                "            \"enabled\": true,\n"
                                "            \"eventType\": \"Instance\",\n"
                                "            \"pattern\": \"TEST_EVENT\\\\s+(?<name>\\\\w+)\",\n"
                                "            \"parameters\": [\"name\"]\n"
                                "          }\n"
                                "        ]\n"
                                "      }\n"
                                "    }\n"
                                "  ]\n"
                                "}";

        auto result = manager.deserializeConfiguration( configContent.toStdString() );

        THEN( "Configuration loads successfully" )
        {
            REQUIRE( result.success );
            REQUIRE( result.logTypeCount == 1 );
            REQUIRE( result.eventRuleCount == 1 );
        }

        AND_WHEN( "Getting event rules from manager" )
        {
            auto rules = manager.getEventRules();

            THEN( "Event rules are available" )
            {
                REQUIRE( rules.size() == 1 );
                REQUIRE( rules[0].name == "test_event" );
                REQUIRE( rules[0].category == "TestCategory" );
                REQUIRE( rules[0].enabled == true );
            }
        }

        AND_WHEN( "Creating a test log file and extracting events" )
        {
            QTemporaryFile logFile( QDir::tempPath() + "/klogg_test_log_XXXXXX.log" );
            REQUIRE( logFile.open() );
            QString logPath = logFile.fileName();

            QTextStream stream( &logFile );
            stream << "2026-03-14T10:00:00.001 INFO Some random log line\n";
            stream << "2026-03-14T10:00:01.002 INFO TEST_EVENT first_event\n";
            stream << "2026-03-14T10:00:02.003 DEBUG Another random line\n";
            stream << "2026-03-14T10:00:03.004 INFO TEST_EVENT second_event\n";
            stream << "2026-03-14T10:00:04.005 WARN More random content\n";
            stream.flush();
            logFile.close();

            auto rules = manager.getEventRules();
            REQUIRE( rules.size() == 1 );

            auto events = manager.extractEventsFromFile( logPath.toStdString(), rules );

            THEN( "Events are extracted with valid timestamps" )
            {
                INFO( "Extracted " << events.size() << " events" );
                REQUIRE( events.size() == 2 );

                REQUIRE( events[0].name == "test_event" );
                REQUIRE( events[0].hasValidTimestamp == true );
                REQUIRE( events[0].timestamp > 0 );
                REQUIRE( events[0].parameters["name"] == "first_event" );

                REQUIRE( events[1].name == "test_event" );
                REQUIRE( events[1].hasValidTimestamp == true );
                REQUIRE( events[1].parameters["name"] == "second_event" );
            }

            AND_THEN( "Events can be converted to Perfetto format" )
            {
                auto perfettoEvents = manager.convertToPerfettoFormat( events );
                REQUIRE( perfettoEvents.size() == 2 );

                REQUIRE( perfettoEvents[0].name == "test_event" );
                REQUIRE( perfettoEvents[0].ph == "i" );
                REQUIRE( perfettoEvents[0].ts > 0 );
            }
        }
    }

    manager.setLogTypes( {} );
}

SCENARIO( "Integration: Slice events with asyncIdParam", "[event][integration]" )
{
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    GIVEN( "A configuration with Slice events using asyncIdParam" )
    {
        QString configContent = "{\n"
                                "  \"logTypes\": [\n"
                                "    {\n"
                                "      \"name\": \"Test Log\",\n"
                                "      \"globPattern\": \"*\",\n"
                                "      \"enabled\": true,\n"
                                "      \"isDefault\": true,\n"
                                "      \"parsingRule\": {\n"
                                "        \"name\": \"Test Format\",\n"
                                "        \"parsePattern\": \"^(?<timestamp>\\\\d{4}-\\\\d{2}-\\\\d{2}T\\\\d{2}:\\\\d{2}:\\\\d{2}\\\\.\\\\d{3})\\\\s+(?<message>.*)$\",\n"
                                "        \"timestampFormat\": \"yyyy-MM-ddTHH:mm:ss.zzz\",\n"
                                "        \"enabled\": true\n"
                                "      },\n"
                                "      \"eventRules\": {\n"
                                "        \"Operations\": [\n"
                                "          {\n"
                                "            \"name\": \"file_op\",\n"
                                "            \"description\": \"File operation\",\n"
                                "            \"enabled\": true,\n"
                                "            \"eventType\": \"Slice\",\n"
                                "            \"beginPattern\": \"BEGIN_OP\\\\s+(?<op_id>\\\\w+)\",\n"
                                "            \"endPattern\": \"END_OP\\\\s+(?<op_id>\\\\w+)\\\\s+duration=(?<dur>\\\\d+)\",\n"
                                "            \"asyncIdParam\": \"op_id\",\n"
                                "            \"parameters\": [\"op_id\"],\n"
                                "            \"endParameters\": [\"op_id\", \"dur\"]\n"
                                "          }\n"
                                "        ]\n"
                                "      }\n"
                                "    }\n"
                                "  ]\n"
                                "}";

        auto result = manager.deserializeConfiguration( configContent.toStdString() );
        REQUIRE( result.success );

        AND_WHEN( "Creating a test log file with Begin/End events" )
        {
            QTemporaryFile logFile( QDir::tempPath() + "/klogg_test_slice_log_XXXXXX.log" );
            REQUIRE( logFile.open() );
            QString logPath = logFile.fileName();

            QTextStream stream( &logFile );
            stream << "2026-03-14T10:00:00.001 BEGIN_OP op1\n";
            stream << "2026-03-14T10:00:00.002 BEGIN_OP op2\n";
            stream << "2026-03-14T10:00:01.001 END_OP op1 duration=1000\n";
            stream << "2026-03-14T10:00:01.002 END_OP op2 duration=1000\n";
            stream.flush();
            logFile.close();

            auto rules = manager.getEventRules();
            auto events = manager.extractEventsFromFile( logPath.toStdString(), rules );

            THEN( "Begin and End events are extracted" )
            {
                REQUIRE( events.size() == 4 );

                int beginCount = 0;
                int endCount = 0;
                for ( const auto& e : events ) {
                    if ( e.sliceType == SliceType::Begin ) beginCount++;
                    if ( e.sliceType == SliceType::End ) endCount++;
                }
                REQUIRE( beginCount == 2 );
                REQUIRE( endCount == 2 );
            }

            AND_THEN( "Events are paired correctly by asyncIdParam" )
            {
                auto perfettoEvents = manager.convertToPerfettoFormat( events );

                int completeCount = 0;
                for ( const auto& e : perfettoEvents ) {
                    if ( e.ph == "X" ) completeCount++;
                }
                REQUIRE( completeCount == 2 );
            }
        }
    }

    manager.setLogTypes( {} );
}
