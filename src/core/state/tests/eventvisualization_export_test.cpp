/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

#include <catch2/catch.hpp>

#include "eventvisualization.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryFile>

#include <chrono>
#include <iostream>

SCENARIO( "EventVisualizationManager JSON export validation", "[event]" )
{
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    GIVEN( "Events with parameters" )
    {
        LogEvent instantEvent;
        instantEvent.name = "http_request";
        instantEvent.category = "network";
        instantEvent.timestamp = 1000000000;
        instantEvent.pid = 1234;
        instantEvent.tid = 5678;
        instantEvent.eventType = EventType::Instance;
        instantEvent.hasValidTimestamp = true;
        instantEvent.parameters["url"] = "/api/users";
        instantEvent.parameters["method"] = "GET";

        std::vector<LogEvent> events = { instantEvent };

        WHEN( "Exporting to Perfetto JSON format" )
        {
            auto perfettoEvents = manager.convertToPerfettoFormat( events );

            QTemporaryFile tempFile( QDir::tempPath() + "/klogg_test_XXXXXX.json" );
            REQUIRE( tempFile.open() );
            QString filename = tempFile.fileName();
            tempFile.close();

            JsonExporter exporter;
            bool success = exporter.exportToFile( filename.toStdString(), perfettoEvents );

            THEN( "Export succeeds with valid JSON structure" )
            {
                REQUIRE( success );

                QFile file( filename );
                REQUIRE( file.open( QIODevice::ReadOnly ) );

                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &parseError );
                REQUIRE( parseError.error == QJsonParseError::NoError );

                QJsonObject root = doc.object();
                REQUIRE( root.contains( "traceEvents" ) );

                QJsonArray traceEvents = root["traceEvents"].toArray();
                REQUIRE( traceEvents.size() == 1 );

                QJsonObject eventObj = traceEvents.at( 0 ).toObject();
                REQUIRE( eventObj["name"].toString() == "http_request" );
                REQUIRE( eventObj["ph"].toString() == "i" );

                QJsonObject args = eventObj["args"].toObject();
                REQUIRE( args["url"].toString() == "/api/users" );
                REQUIRE( args["method"].toString() == "GET" );
            }
        }
    }
}

SCENARIO( "Performance: pre-compiled vs on-the-fly regex", "[event][!benchmark]" )
{
    GIVEN( "A large dataset and event rules" )
    {
        QStringList logLines;
        for ( int i = 0; i < 10000; ++i ) {
            if ( i % 100 == 0 ) {
                logLines.append( QString( "2026-03-12T10:00:%1.000 INFO PATTERN_%2 paramVALUE_%2" )
                                     .arg( i % 60, 2, 10, QChar( '0' ) )
                                     .arg( i / 100 ) );
            }
            else {
                logLines.append( QString( "2026-03-12T10:00:%1.000 DEBUG Random log line %2" )
                                     .arg( i % 60, 2, 10, QChar( '0' ) )
                                     .arg( i ) );
            }
        }

        QStringList patterns;
        for ( int i = 0; i < 10; ++i ) {
            patterns.append( QString( "(?:PATTERN_%1)" ).arg( i ) );
        }
        QString combinedPattern = patterns.join( "|" );

        WHEN( "Using pre-compiled regex" )
        {
            QRegularExpression preCompiled( combinedPattern,
                                            QRegularExpression::CaseInsensitiveOption );

            auto start = std::chrono::high_resolution_clock::now();
            int count = 0;
            for ( const QString& line : logLines ) {
                if ( preCompiled.match( line ).hasMatch() ) {
                    count++;
                }
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>( end - start );

            THEN( "Pre-compiled regex is fast" )
            {
                REQUIRE( count == 100 );
                std::cout << "Pre-compiled regex: " << duration.count() << " us" << std::endl;
            }
        }

        WHEN( "Using on-the-fly regex creation (BAD PRACTICE)" )
        {
            auto start = std::chrono::high_resolution_clock::now();
            int count = 0;
            for ( const QString& line : logLines ) {
                QRegularExpression tempRegex( combinedPattern,
                                              QRegularExpression::CaseInsensitiveOption );
                if ( tempRegex.match( line ).hasMatch() ) {
                    count++;
                }
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>( end - start );

            THEN( "On-the-fly creation is much slower" )
            {
                REQUIRE( count == 100 );
                std::cout << "On-the-fly regex: " << duration.count()
                          << " us (BAD PRACTICE)" << std::endl;
            }
        }
    }
}
