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

// Navigation tests for the QuickFind scanning core. They drive the engine
// synchronously against an in-memory data source, which the extraction from
// QuickFind (previously async + QObject) makes possible.

#include <vector>

#include <QRegularExpression>

#include <catch2/catch.hpp>

#include "atomicflag.h"
#include "fakelinedatasource.h"
#include "quickfindengine.h"
#include "quickfindpattern.h"

namespace {

QuickFindMatcher makeMatcher( const QString& pattern )
{
    QRegularExpression re( QRegularExpression::escape( pattern ),
                           QRegularExpression::UseUnicodePropertiesOption );
    return QuickFindMatcher( !pattern.isEmpty(), re );
}

const QuickFindEngine::ProgressFn noProgress = []( LineNumber, LinesCount ) {};

} // namespace

SCENARIO( "QuickFindEngine forward navigation", "[quickfind][engine]" )
{
    FakeLineDataSource data( { QStringLiteral( "alpha one" ), QStringLiteral( "beta two" ),
                               QStringLiteral( "gamma target three" ),
                               QStringLiteral( "delta four" ),
                               QStringLiteral( "epsilon target five" ) } );
    AtomicFlag interrupt;

    GIVEN( "A pattern present further down the file" )
    {
        auto matcher = makeMatcher( QStringLiteral( "target" ) );

        WHEN( "searching forward from the top" )
        {
            auto result = QuickFindEngine::searchForward( data, matcher, FilePosition{ 0_lnum, 0_lcol },
                                                          interrupt, noProgress );
            THEN( "it finds the first occurrence on line 2" )
            {
                REQUIRE( result.match.isValid() );
                REQUIRE_FALSE( result.interrupted );
                REQUIRE( result.match.line() == 2_lnum );
                REQUIRE( result.match.startColumn() == 6_lcol );
            }
        }

        WHEN( "searching forward starting after the first occurrence" )
        {
            auto result = QuickFindEngine::searchForward( data, matcher, FilePosition{ 2_lnum, 7_lcol },
                                                          interrupt, noProgress );
            THEN( "it finds the second occurrence on line 4" )
            {
                REQUIRE( result.match.isValid() );
                REQUIRE( result.match.line() == 4_lnum );
                REQUIRE( result.match.startColumn() == 8_lcol );
            }
        }
    }

    GIVEN( "A pattern absent from the file" )
    {
        auto matcher = makeMatcher( QStringLiteral( "missing" ) );
        WHEN( "searching forward" )
        {
            auto result = QuickFindEngine::searchForward( data, matcher, FilePosition{ 0_lnum, 0_lcol },
                                                          interrupt, noProgress );
            THEN( "no match, not interrupted" )
            {
                REQUIRE_FALSE( result.match.isValid() );
                REQUIRE_FALSE( result.interrupted );
            }
        }
    }

    GIVEN( "An inactive matcher" )
    {
        QuickFindMatcher matcher;
        WHEN( "searching forward" )
        {
            auto result = QuickFindEngine::searchForward( data, matcher, FilePosition{ 0_lnum, 0_lcol },
                                                          interrupt, noProgress );
            THEN( "no match" )
            {
                REQUIRE_FALSE( result.match.isValid() );
            }
        }
    }
}

SCENARIO( "QuickFindEngine backward navigation", "[quickfind][engine]" )
{
    FakeLineDataSource data( { QStringLiteral( "alpha one" ), QStringLiteral( "beta target two" ),
                               QStringLiteral( "gamma three" ),
                               QStringLiteral( "delta target four" ),
                               QStringLiteral( "epsilon five" ) } );
    AtomicFlag interrupt;

    GIVEN( "A pattern present above the start" )
    {
        auto matcher = makeMatcher( QStringLiteral( "target" ) );

        WHEN( "searching backward from the bottom" )
        {
            auto result = QuickFindEngine::searchBackward(
                data, matcher, FilePosition{ 4_lnum, 0_lcol }, interrupt, noProgress );
            THEN( "it finds the nearest occurrence above, on line 3" )
            {
                REQUIRE( result.match.isValid() );
                REQUIRE( result.match.line() == 3_lnum );
                REQUIRE( result.match.startColumn() == 6_lcol );
            }
        }

        WHEN( "searching backward from between the two occurrences" )
        {
            auto result = QuickFindEngine::searchBackward(
                data, matcher, FilePosition{ 3_lnum, 0_lcol }, interrupt, noProgress );
            THEN( "it finds the earlier occurrence on line 1" )
            {
                REQUIRE( result.match.isValid() );
                REQUIRE( result.match.line() == 1_lnum );
                REQUIRE( result.match.startColumn() == 5_lcol );
            }
        }
    }

    GIVEN( "A pattern absent from the file" )
    {
        auto matcher = makeMatcher( QStringLiteral( "missing" ) );
        WHEN( "searching backward" )
        {
            auto result = QuickFindEngine::searchBackward(
                data, matcher, FilePosition{ 4_lnum, 0_lcol }, interrupt, noProgress );
            THEN( "no match, not interrupted" )
            {
                REQUIRE_FALSE( result.match.isValid() );
                REQUIRE_FALSE( result.interrupted );
            }
        }
    }
}

SCENARIO( "QuickFindEngine honors interruption", "[quickfind][engine]" )
{
    std::vector<QString> manyLines;
    for ( int i = 0; i < 1000; ++i ) {
        manyLines.push_back( QStringLiteral( "plain filler line" ) );
    }
    FakeLineDataSource data( std::move( manyLines ) );

    auto matcher = makeMatcher( QStringLiteral( "never" ) );

    GIVEN( "An interrupt flag that is already set" )
    {
        AtomicFlag interrupt;
        interrupt.set();

        WHEN( "searching forward" )
        {
            auto result = QuickFindEngine::searchForward(
                data, matcher, FilePosition{ 0_lnum, 0_lcol }, interrupt, noProgress );
            THEN( "the scan reports interruption without a match" )
            {
                REQUIRE_FALSE( result.match.isValid() );
                REQUIRE( result.interrupted );
            }
        }
    }
}

SCENARIO( "QuickFindEngine scans across fetch block boundaries", "[quickfind][engine]" )
{
    // Two full blocks plus a non-multiple tail, targets straddling the first
    // block boundary and sitting in the tail block
    const auto blockLines = QuickFindEngine::ScanBlockLines.get();
    const auto nbLines = 2 * blockLines + 1000;

    std::vector<QString> manyLines( static_cast<size_t>( nbLines ),
                                    QStringLiteral( "plain filler line" ) );
    manyLines[ blockLines - 1 ] = QStringLiteral( "target just before the boundary" );
    manyLines[ blockLines ] = QStringLiteral( "target right at the boundary" );
    manyLines[ nbLines - 1 ] = QStringLiteral( "target in the tail block" );
    FakeLineDataSource data( std::move( manyLines ) );
    AtomicFlag interrupt;

    GIVEN( "A pattern with matches around a block boundary" )
    {
        auto matcher = makeMatcher( QStringLiteral( "target" ) );

        WHEN( "searching forward from the top" )
        {
            auto result = QuickFindEngine::searchForward(
                data, matcher, FilePosition{ 0_lnum, 0_lcol }, interrupt, noProgress );
            THEN( "it finds the match on the last line of the first block" )
            {
                REQUIRE( result.match.isValid() );
                REQUIRE( result.match.line() == LineNumber( blockLines - 1 ) );
            }
        }

        WHEN( "searching forward from inside the second block" )
        {
            auto result = QuickFindEngine::searchForward(
                data, matcher, FilePosition{ LineNumber( blockLines + 1 ), 0_lcol }, interrupt,
                noProgress );
            THEN( "it finds the match in the clamped tail block" )
            {
                REQUIRE( result.match.isValid() );
                REQUIRE( result.match.line() == LineNumber( nbLines - 1 ) );
            }
        }

        WHEN( "searching backward from the end" )
        {
            auto result = QuickFindEngine::searchBackward(
                data, matcher, FilePosition{ LineNumber( nbLines - 1 ), 0_lcol }, interrupt,
                noProgress );
            THEN( "it finds the match at the boundary line" )
            {
                REQUIRE( result.match.isValid() );
                REQUIRE( result.match.line() == LineNumber( blockLines ) );
            }
        }
    }
}

SCENARIO( "QuickFindEngine fetches lines in blocks, not one by one", "[quickfind][engine]" )
{
    const auto blockLines = QuickFindEngine::ScanBlockLines.get();
    const auto nbLines = 2 * blockLines + 1000;

    FakeLineDataSource data( std::vector<QString>( static_cast<size_t>( nbLines ),
                                                   QStringLiteral( "plain filler line" ) ) );
    AtomicFlag interrupt;
    auto matcher = makeMatcher( QStringLiteral( "absent" ) );

    WHEN( "a forward scan covers the whole data set" )
    {
        const auto result = QuickFindEngine::searchForward(
            data, matcher, FilePosition{ 0_lnum, 0_lcol }, interrupt, noProgress );

        THEN( "lines are fetched in ScanBlockLines blocks with a clamped tail" )
        {
            REQUIRE_FALSE( result.match.isValid() );
            REQUIRE( data.getLinesCallCount() == 3 );
            REQUIRE( data.lastRequestSize() == 1000 );
            REQUIRE( data.singleLineCallCount() == 0 );
        }
    }

    WHEN( "a backward scan covers the whole data set" )
    {
        const auto result = QuickFindEngine::searchBackward(
            data, matcher, FilePosition{ LineNumber( nbLines - 1 ), 0_lcol }, interrupt,
            noProgress );

        THEN( "lines are fetched in ScanBlockLines blocks" )
        {
            REQUIRE_FALSE( result.match.isValid() );
            REQUIRE( data.getLinesCallCount() == 3 );
            REQUIRE( data.singleLineCallCount() == 0 );
        }
    }
}

SCENARIO( "QuickFindEngine handles an empty data source", "[quickfind][engine]" )
{
    FakeLineDataSource data( {} );
    AtomicFlag interrupt;
    auto matcher = makeMatcher( QStringLiteral( "anything" ) );

    WHEN( "searching forward" )
    {
        const auto result = QuickFindEngine::searchForward(
            data, matcher, FilePosition{ 0_lnum, 0_lcol }, interrupt, noProgress );
        THEN( "no match, no fetch, no crash" )
        {
            REQUIRE_FALSE( result.match.isValid() );
            REQUIRE( data.getLinesCallCount() == 0 );
        }
    }

    WHEN( "searching backward" )
    {
        const auto result = QuickFindEngine::searchBackward(
            data, matcher, FilePosition{ 0_lnum, 0_lcol }, interrupt, noProgress );
        THEN( "no match, no fetch, no crash" )
        {
            REQUIRE_FALSE( result.match.isValid() );
            REQUIRE( data.getLinesCallCount() == 0 );
        }
    }
}
