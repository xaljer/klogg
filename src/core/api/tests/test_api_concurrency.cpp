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

// C4-5 read channel stress: the read-only API methods run on the calling
// thread (KLOGG_READ_CHANNEL=queue falls back to full marshalling, the same
// tests must pass in both modes). These scenarios hammer the read path from
// several threads while writes (marks) and lifecycle operations (cancel,
// close) race on the executor; the assertions are limited to what the
// snapshot semantics guarantee.

#include <catch2/catch.hpp>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QThread>
#include <QUuid>

#include <atomic>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "klogg/api.h"

using namespace klogg::api;

namespace {

constexpr int kWaitTimeoutMs = 20000;
// Rounds of the combined storm + close-race scenario (task requirement: 50)
constexpr int kStressRounds = 50;
constexpr quint64 kStormFileLines = 4000;
constexpr int kReaderThreads = 4;
constexpr int kOpsPerReader = 150;

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
    file.path = QString( "%1/klogg_api_stress_%2.log" )
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

bool waitForSearchDone( Session& session, SearchId id )
{
    QElapsedTimer timer;
    timer.start();
    while ( timer.elapsed() < kWaitTimeoutMs ) {
        const auto progress = session.searchProgress( id );
        if ( !progress ) {
            return false;
        }
        if ( progress->progress >= 1.0f ) {
            return true;
        }
        QThread::msleep( 1 );
    }
    return false;
}

bool isLifecycleError( const Error& error )
{
    return error.code == ErrorCode::FileNotOpen || error.code == ErrorCode::InvalidFileId
           || error.code == ErrorCode::InvalidSearchId || error.code == ErrorCode::SearchCancelled;
}

// One read storm round: readers hit every read method of the file/search
// while a writer thread flips marks. All calls must succeed here (nothing is
// closed yet); content assertions stick to snapshot-safe invariants.
void readStorm( Session& session, FileId fileId, SearchId searchId,
                const std::vector<std::string>& expectedLines, quint64 nbLines,
                std::atomic<uint32_t>& failures )
{
    std::atomic<bool> stopMarks{ false };
    std::thread markWriter( [ & ] {
        std::mt19937 rng( 4242 );
        while ( !stopMarks.load( std::memory_order_relaxed ) ) {
            const auto line
                = LineNumber( rng() % static_cast<LineNumber::UnderlyingType>( nbLines ) );
            session.addMarks( fileId, { line } );
            session.removeMarks( fileId, { line } );
        }
    } );

    std::vector<std::thread> readers;
    for ( int t = 0; t < kReaderThreads; ++t ) {
        readers.emplace_back( [ &, t ] {
            std::mt19937 rng( static_cast<unsigned>( 1000 + t ) );
            for ( int op = 0; op < kOpsPerReader && failures.load() == 0; ++op ) {
                const auto lineIndex
                    = rng() % static_cast<LineNumber::UnderlyingType>( nbLines );
                const auto line = LineNumber( lineIndex );

                // Line text is immutable after indexing: must always match
                const auto oneLine
                    = session.getLines( fileId, line, 1_lcount, AnsiMode::Raw );
                if ( !oneLine || oneLine->size() != 1
                     || oneLine->front().text != expectedLines[ lineIndex ] ) {
                    ++failures;
                }

                const auto batch = session.getLinesBatch( fileId, { line, line + 1_lcount },
                                                          AnsiMode::Strip );
                if ( !batch || batch->size() != 2 ) {
                    ++failures;
                }

                const auto info = session.fileInfo( fileId );
                if ( !info || info->total_lines.get() != nbLines ) {
                    ++failures;
                }

                const auto flags = session.lineFlags( fileId, searchId, line, 4_lcount );
                // Range clamped at the end of the file
                const auto expectedFlags
                    = std::min<quint64>( 4, nbLines - lineIndex );
                if ( !flags || flags->size() != expectedFlags ) {
                    ++failures;
                }
                else {
                    for ( const auto flag : *flags ) {
                        // Only Mark/Match bits can be set in this scenario
                        if ( ( flag & ~( LineFlagMark | LineFlagMatch ) ) != 0 ) {
                            ++failures;
                        }
                    }
                }

                const auto flagsBatch
                    = session.lineFlagsBatch( fileId, searchId, { line, line + 2_lcount } );
                if ( !flagsBatch || flagsBatch->size() != 2 ) {
                    ++failures;
                }

                // Match results must be ascending, in range and really match
                const auto matches = session.searchMatches( searchId, 0, 32 );
                if ( !matches ) {
                    ++failures;
                }
                else {
                    for ( size_t i = 0; i < matches->size(); ++i ) {
                        const auto matched = ( *matches )[ i ];
                        if ( matched.get() >= nbLines
                             || expectedLines[ matched.get() ].find( "error" )
                                    == std::string::npos
                             || ( i > 0 && ( *matches )[ i - 1] >= matched ) ) {
                            ++failures;
                        }
                    }
                }

                const auto count = session.getFilteredLineCount( searchId );
                if ( !count ) {
                    ++failures;
                }

                const auto numbers = session.filteredLineNumbers( searchId, 0, 16 );
                if ( !numbers ) {
                    ++failures;
                }

                const auto filtered
                    = session.getFilteredLines( searchId, 0, 8, AnsiMode::Raw );
                if ( !filtered ) {
                    ++failures;
                }
                else {
                    for ( const auto& filteredLine : *filtered ) {
                        if ( filteredLine.text
                             != expectedLines[ filteredLine.num.get() ] ) {
                            ++failures;
                        }
                    }
                }

                if ( lineIndex > 0 ) {
                    const auto index = session.searchMatchIndex( searchId, line );
                    if ( !index ) {
                        ++failures;
                    }
                    const auto projectionIndex = session.filteredLineIndex( searchId, line );
                    if ( !projectionIndex ) {
                        ++failures;
                    }
                    const auto floorIndex
                        = session.filteredLineIndexAtOrBefore( searchId, line );
                    if ( !floorIndex ) {
                        ++failures;
                    }
                }

                if ( !session.filteredMaxLineLength( searchId ) ) {
                    ++failures;
                }
                if ( !session.searchProgress( searchId ) ) {
                    ++failures;
                }

                const auto marks = session.listMarks( fileId );
                if ( !marks ) {
                    ++failures;
                }
                if ( !session.markAfter( fileId, line ) ) {
                    ++failures;
                }
                if ( !session.markBefore( fileId, line ) ) {
                    ++failures;
                }
            }
        } );
    }

    for ( auto& reader : readers ) {
        reader.join();
    }
    stopMarks.store( true );
    markWriter.join();
}

// Readers hammer the file/search while the main thread cancels the search and
// closes the file; lifecycle errors are the expected outcomes, crashes are not
void closeRace( Session& session, FileId fileId, SearchId searchId, quint64 nbLines,
                std::atomic<uint32_t>& failures )
{
    std::atomic<bool> stop{ false };
    std::vector<std::thread> readers;
    for ( int t = 0; t < 2; ++t ) {
        readers.emplace_back( [ &, t ] {
            std::mt19937 rng( static_cast<unsigned>( 7000 + t ) );
            while ( !stop.load( std::memory_order_relaxed ) && failures.load() == 0 ) {
                const auto line = LineNumber(
                    rng() % static_cast<LineNumber::UnderlyingType>( nbLines ) );
                const auto lines = session.getLines( fileId, line, 2_lcount, AnsiMode::Raw );
                if ( !lines && !isLifecycleError( lines.error() ) ) {
                    ++failures;
                }
                const auto matches = session.searchMatches( searchId, 0, 8 );
                if ( !matches && !isLifecycleError( matches.error() ) ) {
                    ++failures;
                }
                const auto flags = session.lineFlags( fileId, searchId, line, 1_lcount );
                if ( !flags && !isLifecycleError( flags.error() ) ) {
                    ++failures;
                }
                const auto marks = session.listMarks( fileId );
                if ( !marks && !isLifecycleError( marks.error() ) ) {
                    ++failures;
                }
            }
        } );
    }

    // Let the readers spin up, then pull the rug
    QThread::msleep( 2 );
    session.cancelSearch( searchId );
    session.closeFile( fileId );

    stop.store( true );
    for ( auto& reader : readers ) {
        reader.join();
    }

    // After the close the file id is gone for good
    const auto lines = session.getLines( fileId, 0_lnum, 1_lcount, AnsiMode::Raw );
    if ( lines || lines.error().code != ErrorCode::FileNotOpen ) {
        ++failures;
    }
}

} // namespace

TEST_CASE( "API read channel survives concurrent reads, marks writes and close races",
           "[api][concurrency]" )
{
    auto session = createSession();

    std::vector<std::string> expectedLines;
    expectedLines.reserve( kStormFileLines );
    QStringList qLines;
    for ( quint64 i = 0; i < kStormFileLines; ++i ) {
        // Every 7th line matches the search pattern
        std::string line = ( i % 7 == 3 ) ? "error line " + std::to_string( i )
                                          : "info line " + std::to_string( i );
        qLines << QString::fromStdString( line );
        expectedLines.push_back( std::move( line ) );
    }

    std::atomic<uint32_t> failures{ 0 };
    for ( int round = 0; round < kStressRounds; ++round ) {
        const auto file = writeTempLogFile( qLines );

        const auto info = session->openFileSync( file.path.toStdString() );
        REQUIRE( info.has_value() );
        REQUIRE( info->total_lines.get() == kStormFileLines );
        const auto fileId = info->id;

        SearchParams params;
        params.pattern = "error";
        const auto search = session->runSearch( fileId, params );
        REQUIRE( search.has_value() );
        const auto searchId = *search;

        // Reads start while the search is (likely) still running
        readStorm( *session, fileId, searchId, expectedLines, kStormFileLines, failures );
        REQUIRE( waitForSearchDone( *session, searchId ) );

        // With the search complete the match set is stable and fully known
        quint64 expectedMatches = 0;
        for ( quint64 i = 0; i < kStormFileLines; ++i ) {
            if ( i % 7 == 3 ) {
                ++expectedMatches;
            }
        }
        const auto matches = session->searchMatches( searchId, 0, 100000 );
        REQUIRE( matches.has_value() );
        REQUIRE( matches->size() == expectedMatches );
        for ( const auto matched : *matches ) {
            REQUIRE( expectedLines[ matched.get() ].find( "error" ) != std::string::npos );
        }

        closeRace( *session, fileId, searchId, kStormFileLines, failures );

        // The session stays usable for the next round
        const auto files = session->openFiles();
        REQUIRE( files.has_value() );
        REQUIRE( files->empty() );

        if ( failures.load() != 0 ) {
            FAIL( "read channel stress detected "
                  << failures.load() << " failures in round " << round );
        }
    }
}

TEST_CASE( "API read channel: concurrent reads on two files sharing one session",
           "[api][concurrency]" )
{
    auto session = createSession();

    QStringList linesA;
    QStringList linesB;
    for ( int i = 0; i < 2000; ++i ) {
        linesA << QString( "alpha error %1" ).arg( i );
        linesB << QString( "beta %1" ).arg( i );
    }
    const auto fileA = writeTempLogFile( linesA );
    const auto fileB = writeTempLogFile( linesB );

    const auto infoA = session->openFileSync( fileA.path.toStdString() );
    const auto infoB = session->openFileSync( fileB.path.toStdString() );
    REQUIRE( infoA.has_value() );
    REQUIRE( infoB.has_value() );

    SearchParams params;
    params.pattern = "error";
    const auto searchA = session->runSearch( infoA->id, params );
    REQUIRE( searchA.has_value() );
    // Complete the search first so the Match gutter bit assertions below are
    // deterministic
    REQUIRE( waitForSearchDone( *session, *searchA ) );

    std::atomic<uint32_t> failures{ 0 };
    std::vector<std::thread> readers;
    for ( int t = 0; t < 4; ++t ) {
        readers.emplace_back( [ &, t ] {
            const bool useA = ( t % 2 ) == 0;
            const auto fileId = useA ? infoA->id : infoB->id;
            const auto searchId = useA ? *searchA : SearchId{};
            std::mt19937 rng( static_cast<unsigned>( 55 + t ) );
            for ( int op = 0; op < 100 && failures.load() == 0; ++op ) {
                const auto line = LineNumber( rng() % 2000 );
                const auto lines = session->getLines( fileId, line, 1_lcount, AnsiMode::Raw );
                if ( !lines || lines->size() != 1 ) {
                    ++failures;
                }
                if ( useA ) {
                    const auto matches = session->searchMatches( searchId, 0, 4 );
                    if ( !matches ) {
                        ++failures;
                    }
                    const auto flags = session->lineFlags( fileId, searchId, line, 1_lcount );
                    if ( !flags || flags->size() != 1
                         || ( flags->front() & LineFlagMatch ) == 0 ) {
                        ++failures;
                    }
                }
            }
        } );
    }
    for ( auto& reader : readers ) {
        reader.join();
    }

    REQUIRE( failures.load() == 0 );

    REQUIRE( session->cancelSearch( *searchA ).has_value() );
    REQUIRE( session->closeFile( infoA->id ).has_value() );
    REQUIRE( session->closeFile( infoB->id ).has_value() );
}
