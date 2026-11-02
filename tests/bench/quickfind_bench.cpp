/*
 * Copyright (C) 2026 klogg contributors
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

// Baseline benchmark for QuickFind. Captures the costs the refactor targets:
//  - forward scan cost per line (worst case: scanning for an absent term)
//  - backward matching on a pathological many-match line
//  - per-frame highlight recompute cost (QuickFindPattern::matchLine over a
//    screenful of lines) -- the work the highlight cache is meant to remove.
//  - full-scan throughput through the view's LineDataSource (B6): the pre-B6
//    per-line native path (replica loop) vs the block-fetching engine over a
//    native source and over the Core API session (the LogMainView path)

#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QSignalSpy>
#include <QString>
#include <QTextStream>
#include <QTimer>
#include <QUuid>

#include <logger.h>

#include "ansi_parser.h"
#include "configuration.h"
#include "highlightedmatch.h"
#include "klogg/api_session.h"
#include "linedatasource.h"
#include "linetypes_qt.h"
#include "logdata.h"
#include "persistentinfo.h"
#include "quickfindengine.h"
#include "quickfindpattern.h"

const bool PersistentInfo::ForcePortable = true;

namespace {

PostTarget testPostTarget()
{
    return []( std::function<void()> task ) {
        QMetaObject::invokeMethod( QCoreApplication::instance(), std::move( task ),
                                   Qt::QueuedConnection );
    };
}

std::vector<QString> makePlainLines( size_t count )
{
    static const std::vector<QString> templates = {
        QStringLiteral( "2024-01-15 12:34:56.789 [INFO] [thread-5] Request processed successfully" ),
        QStringLiteral( "2024-01-15 12:34:56.890 [DEBUG] [thread-12] Cache miss for key user_3421" ),
        QStringLiteral( "2024-01-15 12:34:57.001 [WARN] [thread-3] Slow query detected: 1203ms" ),
        QStringLiteral( "2024-01-15 12:34:57.100 [INFO] [thread-8] Connection pool size now 42" ),
    };
    std::mt19937 rng( 42 );
    std::uniform_int_distribution<size_t> dist( 0, templates.size() - 1 );
    std::vector<QString> lines;
    lines.reserve( count );
    for ( size_t i = 0; i < count; ++i ) {
        lines.push_back( templates[ dist( rng ) ] );
    }
    return lines;
}

double timeUs( const std::function<void()>& fn )
{
    const auto t0 = std::chrono::steady_clock::now();
    fn();
    const auto t1 = std::chrono::steady_clock::now();
    return static_cast<double>(
               std::chrono::duration_cast<std::chrono::nanoseconds>( t1 - t0 ).count() )
           / 1000.0;
}

void benchForwardScan()
{
    constexpr size_t kLines = 500000;
    const auto lines = makePlainLines( kLines );

    QRegularExpression absent( QStringLiteral( "ZZZ_no_such_token_ZZZ" ) );
    const QuickFindMatcher matcher( true, absent );

    const double usRegexOnly = timeUs( [ & ] {
        for ( const auto& line : lines ) {
            volatile bool m = matcher.isLineMatching( line );
            (void)m;
        }
    } );

    const double usWithAnsi = timeUs( [ & ] {
        for ( const auto& line : lines ) {
            auto text = QString::fromStdString(
                AnsiSgrParser::processDisplayLine( line.toStdString(),
                                                   Configuration::get().ansiProcessing(), klogg::Rgba(),
                                                   klogg::Rgba() )
                    .text );
            volatile bool m = matcher.isLineMatching( text );
            (void)m;
        }
    } );

    std::cout << std::left << std::setw( 44 ) << "Forward scan, absent term (regex only)"
              << std::right << std::setw( 12 ) << static_cast<int64_t>( usRegexOnly ) << " us"
              << std::setw( 10 ) << std::fixed << std::setprecision( 1 )
              << usRegexOnly * 1000.0 / kLines << " ns/line" << std::endl;
    std::cout << std::left << std::setw( 44 ) << "Forward scan, absent term (+ANSI display)"
              << std::right << std::setw( 12 ) << static_cast<int64_t>( usWithAnsi ) << " us"
              << std::setw( 10 ) << std::fixed << std::setprecision( 1 )
              << usWithAnsi * 1000.0 / kLines << " ns/line" << std::endl;
}

void benchBackwardPathological()
{
    constexpr int kRounds = 2000;
    QString line( 10000, QLatin1Char( 'a' ) );

    QRegularExpression re( QStringLiteral( "a" ) );
    const QuickFindMatcher regexMatcher( true, re );
    const QuickFindMatcher plainMatcher( true, re, QStringLiteral( "a" ), Qt::CaseSensitive );

    const double usRegex = timeUs( [ & ] {
        for ( int i = 0; i < kRounds; ++i ) {
            volatile bool m = regexMatcher.isLineMatchingBackward( line );
            (void)m;
        }
    } );

    const double usPlain = timeUs( [ & ] {
        for ( int i = 0; i < kRounds; ++i ) {
            volatile bool m = plainMatcher.isLineMatchingBackward( line );
            (void)m;
        }
    } );

    std::cout << std::left << std::setw( 44 ) << "Backward 10k-match line (regex globalMatch)"
              << std::right << std::setw( 12 ) << static_cast<int64_t>( usRegex ) << " us"
              << std::setw( 10 ) << std::fixed << std::setprecision( 1 )
              << usRegex * 1000.0 / kRounds << " ns/call" << std::endl;
    std::cout << std::left << std::setw( 44 ) << "Backward 10k-match line (plain lastIndexOf)"
              << std::right << std::setw( 12 ) << static_cast<int64_t>( usPlain ) << " us"
              << std::setw( 10 ) << std::fixed << std::setprecision( 1 )
              << usPlain * 1000.0 / kRounds << " ns/call" << std::endl;
}

void benchHighlightRecompute()
{
    constexpr int kVisibleLines = 100;
    constexpr int kFrames = 10000;

    const auto lines = makePlainLines( kVisibleLines );

    // The caller now resolves the match color once per frame and passes it in,
    // instead of matchLine resolving it per line.
    const QColor backColor = Configuration::get().qfBackColor();

    // Worst case: pattern matches every visible line ("thread" appears in all).
    QuickFindPattern matchAll;
    matchAll.changeSearchPattern( QStringLiteral( "thread" ), false, false );

    // Typical idle case: pattern matches none of the visible lines.
    QuickFindPattern matchNone;
    matchNone.changeSearchPattern( QStringLiteral( "zzz_absent_zzz" ), false, false );

    klogg::vector<HighlightedMatch> matches;

    const double usAll = timeUs( [ & ] {
        for ( int f = 0; f < kFrames; ++f ) {
            for ( const auto& line : lines ) {
                matchAll.matchLine( line, backColor, matches );
            }
        }
    } );

    const double usNone = timeUs( [ & ] {
        for ( int f = 0; f < kFrames; ++f ) {
            for ( const auto& line : lines ) {
                matchNone.matchLine( line, backColor, matches );
            }
        }
    } );

    const auto report = [ & ]( const char* name, double us ) {
        const double usPerFrame = us / kFrames;
        std::cout << std::left << std::setw( 44 ) << name << std::right << std::setw( 12 )
                  << static_cast<int64_t>( us ) << " us" << std::setw( 10 ) << std::fixed
                  << std::setprecision( 2 ) << usPerFrame << " us/frame" << std::endl;
    };

    report( "Highlight recompute, all lines match", usAll );
    report( "Highlight recompute, no lines match", usNone );
}

//
// B6 data-source channels
//

// LineDataSource forwarding to a native data set, same shape as the
// AbstractLogView base implementation
class NativeLineSource : public LineDataSource {
public:
    explicit NativeLineSource( const AbstractLogData& data )
        : data_( data )
    {
    }

    std::string getLineString( LineNumber line ) const override
    {
        return data_.getLineString( line );
    }
    std::string getExpandedLineString( LineNumber line ) const override
    {
        return data_.getExpandedLineString( line );
    }
    klogg::vector<std::string> getLines( LineNumber firstLine, LinesCount number ) const override
    {
        return data_.getLines( firstLine, number );
    }
    LineNumber getLineNumber( LineNumber index ) const override
    {
        return data_.getLineNumber( index );
    }
    LinesCount getNbLine() const override
    {
        return data_.getNbLine();
    }
    LineLength getMaxLength() const override
    {
        return data_.getMaxLength();
    }
    LineLength getLineLength( LineNumber line ) const override
    {
        return data_.getLineLength( line );
    }

private:
    const AbstractLogData& data_;
};

// LineDataSource fetching through the Core API session, mirroring the
// LogMainView overrides (raw text, consumer-side decode and padding)
class ApiLineSource : public LineDataSource {
public:
    ApiLineSource( klogg::api::Session& session, klogg::api::FileId fileId, LinesCount nbLines )
        : session_( session )
        , fileId_( fileId )
        , nbLines_( nbLines )
    {
    }

    std::string getLineString( LineNumber line ) const override
    {
        const auto lines = getLines( line, 1_lcount );
        return lines.empty() ? std::string{} : lines.front();
    }
    std::string getExpandedLineString( LineNumber line ) const override
    {
        return untabify( getLineString( line ) );
    }
    klogg::vector<std::string> getLines( LineNumber firstLine, LinesCount number ) const override
    {
        const auto result
            = session_.getLines( fileId_, firstLine, number, klogg::api::AnsiMode::Raw );
        klogg::vector<std::string> lines;
        if ( result ) {
            lines.reserve( result->size() );
            for ( const auto& line : *result ) {
                lines.push_back( line.text );
            }
        }
        while ( lines.size() < number.get() ) {
            lines.push_back( std::string{} );
        }
        return lines;
    }
    LineNumber getLineNumber( LineNumber index ) const override
    {
        return index + 1_lcount;
    }
    LinesCount getNbLine() const override
    {
        return nbLines_;
    }
    LineLength getMaxLength() const override
    {
        return LineLength{ 0 };
    }
    LineLength getLineLength( LineNumber line ) const override
    {
        return LineLength{ static_cast<LineLength::UnderlyingType>( getExpandedLineString( line ).size() ) };
    }

private:
    klogg::api::Session& session_;
    klogg::api::FileId fileId_;
    LinesCount nbLines_;
};

// Replica of the pre-B6 scan path: the engine loop as it was, fetching every
// line one by one straight from the native data set
Portion legacyScanForward( const AbstractLogData& data, const QuickFindMatcher& matcher )
{
    auto line = 0_lnum;
    const auto nbLines = data.getNbLine();
    while ( line < nbLines ) {
        const auto text = QString::fromStdString(
            AnsiSgrParser::processDisplayLine( data.getExpandedLineString( line ),
                                               Configuration::get().ansiProcessing(), klogg::Rgba(),
                                               klogg::Rgba() )
                .text );
        if ( matcher.isLineMatching( text ) ) {
            const auto [ startCol, endCol ] = matcher.getLastMatch();
            return Portion{ line, startCol, endCol };
        }
        ++line;
    }
    return {};
}

constexpr uint64_t ChannelFileLines = 100000;
constexpr uint64_t SentinelLineIndex = 50123;

QString generateChannelFile()
{
    const auto filePath
        = QStringLiteral( "%1/klogg_quickfind_bench_%2.log" )
              .arg( QDir::tempPath(), QUuid::createUuid().toString( QUuid::WithoutBraces ) );

    QFile file( filePath );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) ) {
        std::cerr << "Failed to create temp log file: " << filePath.toStdString() << std::endl;
        return {};
    }

    QTextStream stream( &file );
    for ( uint64_t lineIndex = 0; lineIndex < ChannelFileLines; ++lineIndex ) {
        if ( lineIndex == SentinelLineIndex ) {
            stream << "ZZZ_quickfind_bench_sentinel_ZZZ\n";
        }
        else {
            stream << QStringLiteral(
                          "2026-08-09 12:00:%1.%2 [INFO] [thread-%3] some message text line %4\n" )
                          .arg( lineIndex % 60, 2, 10, QLatin1Char( '0' ) )
                          .arg( lineIndex % 1000, 3, 10, QLatin1Char( '0' ) )
                          .arg( lineIndex % 32 )
                          .arg( lineIndex );
        }
    }
    file.close();

    std::cerr << "Generated " << filePath.toStdString() << " (" << QFileInfo( filePath ).size()
              << " bytes, " << ChannelFileLines << " lines)" << std::endl;
    return filePath;
}

void reportScan( const char* name, double us, uint64_t lines )
{
    std::cout << std::left << std::setw( 44 ) << name << std::right << std::setw( 12 )
              << static_cast<int64_t>( us ) << " us" << std::setw( 10 ) << std::fixed
              << std::setprecision( 1 ) << us * 1000.0 / static_cast<double>( lines ) << " ns/line"
              << std::setw( 12 )
              << static_cast<int64_t>( us > 0 ? static_cast<double>( lines ) * 1e6 / us : 0 )
              << " lines/s" << std::endl;
}

void benchDataSourceChannels()
{
    const auto filePath = generateChannelFile();
    if ( filePath.isEmpty() ) {
        return;
    }

    auto logData = std::make_unique<LogData>( testPostTarget() );
    bool loadFinished = false;
    const auto listenerId
        = logData->addLoadingFinishedListener( [ &loadFinished ]( LoadingStatus ) {
              loadFinished = true;
          } );
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
        return;
    }
    logData->removeLoadingFinishedListener( listenerId );

    QRegularExpression absentRe( QStringLiteral( "ZZZ_no_such_token_ZZZ" ) );
    const QuickFindMatcher absentMatcher( true, absentRe );
    const QuickFindEngine::ProgressFn noProgress = []( LineNumber, LinesCount ) {};
    const AtomicFlag noInterrupt;

    const NativeLineSource nativeSource( *logData );
    size_t matchCount = 0;

    const double usLegacy = timeUs( [ & ] {
        const auto result = legacyScanForward( *logData, absentMatcher );
        matchCount += result.isValid() ? 1u : 0u;
    } );
    reportScan( "Full scan, absent term (pre-B6 per-line native)", usLegacy, ChannelFileLines );

    const double usNative = timeUs( [ & ] {
        const auto result = QuickFindEngine::searchForward(
            nativeSource, absentMatcher, FilePosition{ 0_lnum, 0_lcol }, noInterrupt, noProgress );
        matchCount += result.match.isValid() ? 1u : 0u;
    } );
    reportScan( "Full scan, absent term (B6 engine, native source)", usNative, ChannelFileLines );

    auto session = klogg::api::Session::create();
    if ( !session ) {
        std::cerr << "Failed to create API session" << std::endl;
        QFile::remove( filePath );
        return;
    }
    const auto opened = ( *session )->openFileSync( filePath.toStdString() );
    if ( !opened ) {
        std::cerr << "API open failed: " << opened.error().message << std::endl;
        QFile::remove( filePath );
        return;
    }

    ApiLineSource apiSource( **session, opened->id, opened->total_lines );
    const double usApi = timeUs( [ & ] {
        const auto result = QuickFindEngine::searchForward(
            apiSource, absentMatcher, FilePosition{ 0_lnum, 0_lcol }, noInterrupt, noProgress );
        matchCount += result.match.isValid() ? 1u : 0u;
    } );
    reportScan( "Full scan, absent term (B6 engine, API source)", usApi, ChannelFileLines );

    // Same match through all three channels proves the switch is faithful
    QuickFindPattern sentinelPattern;
    sentinelPattern.changeSearchPattern( QStringLiteral( "ZZZ_quickfind_bench_sentinel_ZZZ" ),
                                         false, false );
    const auto sentinelMatcher = sentinelPattern.getMatcher();

    const auto legacyHit = legacyScanForward( *logData, sentinelMatcher );
    const auto nativeHit
        = QuickFindEngine::searchForward( nativeSource, sentinelMatcher,
                                          FilePosition{ 0_lnum, 0_lcol }, noInterrupt, noProgress )
              .match;
    const auto apiHit
        = QuickFindEngine::searchForward( apiSource, sentinelMatcher,
                                          FilePosition{ 0_lnum, 0_lcol }, noInterrupt, noProgress )
              .match;

    const bool agree = legacyHit.isValid() && nativeHit.isValid() && apiHit.isValid()
                       && legacyHit.line() == nativeHit.line() && nativeHit.line() == apiHit.line()
                       && legacyHit.startColumn() == nativeHit.startColumn()
                       && nativeHit.startColumn() == apiHit.startColumn()
                       && nativeHit.line() == LineNumber( SentinelLineIndex );
    std::cout << std::left << std::setw( 44 ) << "Sentinel match, all channels agree" << std::right
              << std::setw( 12 ) << ( agree ? "OK" : "MISMATCH" ) << " line "
              << ( nativeHit.isValid() ? static_cast<int64_t>( nativeHit.line().get() ) : -1 )
              << std::endl;

    ( *session )->closeFile( opened->id );
    QFile::remove( filePath );
}

} // namespace

int main( int argc, char* argv[] )
{
    QApplication app( argc, argv );
    logging::enableLogging();
    // Composition root: the store location has to be selected before the first
    // store access, which Configuration::getSynced() performs
    PersistentInfo::initialize();
    Configuration::getSynced();

    std::cout << "QuickFind baseline benchmark" << std::endl;
    std::cout << std::string( 80, '=' ) << std::endl;

    benchForwardScan();
    benchBackwardPathological();
    benchHighlightRecompute();
    benchDataSourceChannels();

    return 0;
}
