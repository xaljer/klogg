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

// Integration tests for the asynchronous QuickFind orchestration: results are
// delivered through the event loop, and rapid incremental requests must never
// block the caller nor produce a wrong final result.

#include <functional>
#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>

#include <catch2/catch.hpp>

#include "fakelogdata.h"
#include "quickfind.h"
#include "quickfindpattern.h"
#include "selection.h"

namespace {

bool waitFor( const std::function<bool()>& predicate, int timeoutMs = 5000 )
{
    QElapsedTimer timer;
    timer.start();
    while ( !predicate() && timer.elapsed() < timeoutMs ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    }
    return predicate();
}

QuickFindMatcher makeMatcher( const QString& text )
{
    QuickFindPattern pattern;
    pattern.changeSearchPattern( text, false, false );
    return pattern.getMatcher();
}

std::vector<QString> makeLines( int count, const QString& fill )
{
    std::vector<QString> lines;
    lines.reserve( static_cast<size_t>( count ) );
    for ( int i = 0; i < count; ++i ) {
        lines.push_back( fill );
    }
    return lines;
}

} // namespace

SCENARIO( "QuickFind delivers a forward result through the event loop", "[quickfind][async]" )
{
    FakeLogData data( { QStringLiteral( "alpha" ), QStringLiteral( "beta" ),
                        QStringLiteral( "gamma target" ), QStringLiteral( "delta" ) } );
    QuickFind qf( data );
    auto matcher = makeMatcher( QStringLiteral( "target" ) );

    std::vector<std::pair<bool, Portion>> results;
    QObject::connect( &qf, &QuickFind::searchDone,
                      [ &results ]( bool hasMatch, Portion p ) {
                          results.emplace_back( hasMatch, p );
                      } );

    WHEN( "searching forward from the top" )
    {
        qf.searchForward( Selection{}, matcher );

        THEN( "the match on line 2 arrives asynchronously" )
        {
            REQUIRE( waitFor( [ & ] { return !results.empty(); } ) );
            REQUIRE( results.back().first );
            REQUIRE( results.back().second.line() == 2_lnum );
        }
    }
}

SCENARIO( "QuickFind reports no match without blocking", "[quickfind][async]" )
{
    FakeLogData data( makeLines( 5000, QStringLiteral( "filler line" ) ) );
    QuickFind qf( data );
    auto matcher = makeMatcher( QStringLiteral( "absent" ) );

    bool done = false;
    bool hadMatch = true;
    QObject::connect( &qf, &QuickFind::searchDone, [ & ]( bool hasMatch, Portion ) {
        done = true;
        hadMatch = hasMatch;
    } );

    WHEN( "searching for an absent term" )
    {
        qf.searchForward( Selection{}, matcher );

        THEN( "the call returns immediately and a no-match result arrives" )
        {
            // The call above already returned (no waitForFinished); confirm the
            // asynchronous result reports no match.
            REQUIRE( waitFor( [ & ] { return done; } ) );
            REQUIRE_FALSE( hadMatch );
        }
    }
}

SCENARIO( "QuickFind coalesces rapid incremental requests", "[quickfind][async]" )
{
    // Target lines are chosen so each pattern resolves to a distinct line.
    std::vector<QString> lines = makeLines( 20000, QStringLiteral( "noise noise noise" ) );
    lines[ 5000 ] = QStringLiteral( "aaa here" );
    lines[ 9000 ] = QStringLiteral( "bbb here" );
    lines[ 12000 ] = QStringLiteral( "ccc here" );
    FakeLogData data( std::move( lines ) );

    QuickFind qf( data );
    auto matcherA = makeMatcher( QStringLiteral( "aaa" ) );
    auto matcherB = makeMatcher( QStringLiteral( "bbb" ) );
    auto matcherC = makeMatcher( QStringLiteral( "ccc" ) );

    std::vector<std::pair<bool, Portion>> results;
    QObject::connect( &qf, &QuickFind::searchDone,
                      [ &results ]( bool hasMatch, Portion p ) {
                          results.emplace_back( hasMatch, p );
                      } );

    WHEN( "three incremental searches are fired back-to-back (simulating typing)" )
    {
        qf.incrementallySearchForward( Selection{}, matcherA );
        qf.incrementallySearchForward( Selection{}, matcherB );
        qf.incrementallySearchForward( Selection{}, matcherC );

        THEN( "the final delivered result matches the last pattern, on line 12000" )
        {
            const auto isFinal = [ & ] {
                return !results.empty() && results.back().first
                       && results.back().second.line() == 12000_lnum;
            };
            REQUIRE( waitFor( isFinal ) );
        }
    }
}

