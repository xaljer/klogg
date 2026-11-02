/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
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

// Engine performance baseline: indexing throughput, search latency and getLines
// throughput measured directly against LogData/LogFilteredData (no API layer).
// The input file is 100MB instead of the suggested 200MB to keep generation time
// bounded on slow disks; file generation time is not part of any measurement.
// Output is one `metric,value,unit` line per measurement for archival diffing.

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QString>
#include <QTextStream>
#include <QTimer>
#include <QUuid>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <tuple>
#include <vector>

#include "configuration.h"
#include "klogg/api_session.h"
#include "linetypes.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "persistentinfo.h"
#include "regularexpressionpattern.h"

// Each binary defines this itself (same as tests_main.cpp); portable mode keeps
// QSettings data next to the exe instead of touching the user profile
const bool PersistentInfo::ForcePortable = true;

namespace {

constexpr int64_t TargetFileSizeBytes = 100ll * 1024 * 1024;
constexpr int WaitSliceMs = 60000;
constexpr LinesCount GetLinesBatch = 100_lcount;

using SearchProgressCall = std::tuple<LinesCount, int, LineNumber>;

PostTarget testPostTarget()
{
    return []( std::function<void()> task ) {
        QMetaObject::invokeMethod( QCoreApplication::instance(), std::move( task ),
                                   Qt::QueuedConnection );
    };
}

// ~8% ERROR lines so the search match count stays meaningful
const char* levelForLine( uint64_t lineIndex )
{
    switch ( lineIndex % 100 ) {
    case 0:
    case 1:
        return "FATAL";
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
        return "ERROR";
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
        return "WARN";
    default:
        return ( lineIndex % 2 == 0 ) ? "INFO" : "DEBUG";
    }
}

QString generateLogFile()
{
    const auto filePath
        = QStringLiteral( "%1/klogg_engine_bench_%2.log" )
              .arg( QDir::tempPath(), QUuid::createUuid().toString( QUuid::WithoutBraces ) );

    QFile file( filePath );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) ) {
        std::cerr << "Failed to create temp log file: " << filePath.toStdString() << std::endl;
        return {};
    }

    QTextStream stream( &file );
    uint64_t lineIndex = 0;
    while ( file.size() < TargetFileSizeBytes ) {
        stream << QStringLiteral(
                      "2026-07-19 12:00:%1.%2 [%3] [thread-%4] some message text line %5\n" )
                      .arg( lineIndex % 60, 2, 10, QLatin1Char( '0' ) )
                      .arg( lineIndex % 1000, 3, 10, QLatin1Char( '0' ) )
                      .arg( levelForLine( lineIndex ) )
                      .arg( lineIndex % 32 )
                      .arg( lineIndex );
        ++lineIndex;
    }
    file.close();

    std::cerr << "Generated " << filePath.toStdString() << " (" << QFileInfo( filePath ).size()
              << " bytes, " << lineIndex << " lines)" << std::endl;
    return filePath;
}

bool waitForSearchCompletion( const std::vector<SearchProgressCall>& calls )
{
    auto isCompleted = [ &calls ]() {
        return !calls.empty() && std::get<1>( calls.back() ) == 100;
    };

    const auto deadline
        = std::chrono::steady_clock::now() + std::chrono::milliseconds( WaitSliceMs );
    while ( !isCompleted() ) {
        if ( std::chrono::steady_clock::now() >= deadline ) {
            return false;
        }
        QEventLoop loop;
        QTimer::singleShot( 100, &loop, &QEventLoop::quit );
        loop.exec();
    }
    return true;
}

void printMetric( const char* name, double value, const char* unit )
{
    std::cout << name << ',' << value << ',' << unit << std::endl;
}

double mbPerSecond( int64_t bytes, qint64 elapsedMs )
{
    return elapsedMs > 0 ? static_cast<double>( bytes ) / ( 1024.0 * 1024.0 )
                               / ( static_cast<double>( elapsedMs ) / 1000.0 )
                         : 0.0;
}

} // namespace

int main( int argc, char* argv[] )
{
    QCoreApplication::setOrganizationName( QStringLiteral( "klogg" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "klogg_engine_bench" ) );
    QCoreApplication app( argc, argv );

    // Composition root: the store location has to be selected before the first
    // store access (LogData's constructor requires the Configuration
    // persistable initialized)
    PersistentInfo::initialize();
    Configuration::getSynced();

    const auto filePath = generateLogFile();
    if ( filePath.isEmpty() ) {
        return EXIT_FAILURE;
    }
    const int64_t fileSizeBytes = QFileInfo( filePath ).size();

    {
        auto logData = std::make_unique<LogData>( testPostTarget() );
        bool loadFinished = false;
        const auto listenerId
            = logData->addLoadingFinishedListener( [ &loadFinished ]( LoadingStatus ) {
                  loadFinished = true;
              } );

        QElapsedTimer timer;
        timer.start();
        logData->attachFile( filePath.toStdString() );
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 60 );
        while ( !loadFinished && std::chrono::steady_clock::now() < deadline ) {
            QEventLoop loop;
            QTimer::singleShot( 100, &loop, &QEventLoop::quit );
            loop.exec();
        }
        if ( !loadFinished ) {
            std::cerr << "Indexing did not finish in time" << std::endl;
            QFile::remove( filePath );
            return EXIT_FAILURE;
        }
        logData->removeLoadingFinishedListener( listenerId );
        const auto indexMs = timer.elapsed();

        printMetric( "index_time", static_cast<double>( indexMs ), "ms" );
        printMetric( "index_size", static_cast<double>( fileSizeBytes ), "bytes" );
        printMetric( "index_throughput", mbPerSecond( fileSizeBytes, indexMs ), "MB/s" );

        auto filteredData = logData->getNewFilteredData();
        std::vector<SearchProgressCall> searchProgressCalls;
        filteredData->addMatchesSearchProgressedListener(
            [ &searchProgressCalls ]( LinesCount nbMatches, int progress, LineNumber initialLine ) {
                searchProgressCalls.emplace_back( nbMatches, progress, initialLine );
            } );

        timer.start();
        filteredData->runSearch(
            RegularExpressionPattern( "ERROR", true, false, false, false ) );
        if ( !waitForSearchCompletion( searchProgressCalls ) ) {
            std::cerr << "Search did not finish in time" << std::endl;
            QFile::remove( filePath );
            return EXIT_FAILURE;
        }
        const auto searchMs = timer.elapsed();
        const auto matches = filteredData->getNbMatches().get();

        printMetric( "search_time", static_cast<double>( searchMs ), "ms" );
        printMetric( "search_matches", static_cast<double>( matches ), "count" );
        printMetric( "search_throughput", mbPerSecond( fileSizeBytes, searchMs ), "MB/s" );

        const auto nbLines = logData->getNbLine().get();
        uint64_t fetchedLines = 0;
        uint64_t fetchedChars = 0;
        timer.start();
        while ( fetchedLines < nbLines ) {
            const auto lines = logData->getLines( LineNumber( fetchedLines ), GetLinesBatch );
            if ( lines.empty() ) {
                break;
            }
            for ( const auto& line : lines ) {
                fetchedChars += static_cast<uint64_t>( line.size() );
            }
            fetchedLines += lines.size();
        }
        const auto getLinesMs = timer.elapsed();

        printMetric( "getlines_time", static_cast<double>( getLinesMs ), "ms" );
        printMetric( "getlines_lines", static_cast<double>( fetchedLines ), "count" );
        printMetric( "getlines_chars", static_cast<double>( fetchedChars ), "chars" );
        printMetric( "getlines_throughput",
                     getLinesMs > 0 ? static_cast<double>( fetchedLines )
                                          / ( static_cast<double>( getLinesMs ) / 1000.0 )
                                    : 0.0,
                     "lines/s" );
    }

    // Same getLines loop through a Core API session (B4): the consumer-side
    // decode (std::string -> QString) is included, as the Qt views pay it
    {
        auto session = klogg::api::Session::create();
        if ( !session ) {
            std::cerr << "Failed to create API session" << std::endl;
            QFile::remove( filePath );
            return EXIT_FAILURE;
        }

        const auto opened = ( *session )->openFileSync( filePath.toStdString() );
        if ( !opened ) {
            std::cerr << "API open failed: " << opened.error().message << std::endl;
            QFile::remove( filePath );
            return EXIT_FAILURE;
        }

        const auto nbLines = opened->total_lines.get();
        uint64_t fetchedLines = 0;
        uint64_t fetchedChars = 0;
        QElapsedTimer timer;
        timer.start();
        while ( fetchedLines < nbLines ) {
            const auto lines = ( *session )
                                   ->getLines( opened->id, LineNumber( fetchedLines ),
                                               GetLinesBatch, klogg::api::AnsiMode::Raw );
            if ( !lines || lines->empty() ) {
                break;
            }
            for ( const auto& line : *lines ) {
                fetchedChars += static_cast<uint64_t>( QString::fromStdString( line.text ).size() );
            }
            fetchedLines += lines->size();
        }
        const auto getLinesApiMs = timer.elapsed();

        printMetric( "getlines_api_time", static_cast<double>( getLinesApiMs ), "ms" );
        printMetric( "getlines_api_lines", static_cast<double>( fetchedLines ), "count" );
        printMetric( "getlines_api_chars", static_cast<double>( fetchedChars ), "chars" );
        printMetric( "getlines_api_throughput",
                     getLinesApiMs > 0 ? static_cast<double>( fetchedLines )
                                             / ( static_cast<double>( getLinesApiMs ) / 1000.0 )
                                       : 0.0,
                     "lines/s" );

        // Large-batch pass: amortizes the fixed per-call marshalling cost to
        // separate it from the per-line conversion cost
        fetchedLines = 0;
        fetchedChars = 0;
        timer.start();
        while ( fetchedLines < nbLines ) {
            const auto lines = ( *session )
                                   ->getLines( opened->id, LineNumber( fetchedLines ),
                                               LinesCount( 5000 ), klogg::api::AnsiMode::Raw );
            if ( !lines || lines->empty() ) {
                break;
            }
            for ( const auto& line : *lines ) {
                fetchedChars += static_cast<uint64_t>( QString::fromStdString( line.text ).size() );
            }
            fetchedLines += lines->size();
        }
        const auto getLinesApiBatchMs = timer.elapsed();

        printMetric( "getlines_api_batch_time", static_cast<double>( getLinesApiBatchMs ), "ms" );
        printMetric( "getlines_api_batch_throughput",
                     getLinesApiBatchMs > 0 ? static_cast<double>( fetchedLines )
                                                  / ( static_cast<double>( getLinesApiBatchMs )
                                                      / 1000.0 )
                                            : 0.0,
                     "lines/s" );

        ( *session )->closeFile( opened->id );
    }

    QFile::remove( filePath );
    return EXIT_SUCCESS;
}
