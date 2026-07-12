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
#include "fakelogdata.h"
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
    FakeLogData data( { QStringLiteral( "alpha one" ), QStringLiteral( "beta two" ),
                        QStringLiteral( "gamma target three" ), QStringLiteral( "delta four" ),
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
    FakeLogData data( { QStringLiteral( "alpha one" ), QStringLiteral( "beta target two" ),
                        QStringLiteral( "gamma three" ), QStringLiteral( "delta target four" ),
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
    FakeLogData data( std::move( manyLines ) );

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
