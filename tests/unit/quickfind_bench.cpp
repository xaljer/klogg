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

#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <QApplication>
#include <QString>

#include <logger.h>

#include "ansi_parser.h"
#include "configuration.h"
#include "highlightedmatch.h"
#include "linetypes.h"
#include "persistentinfo.h"
#include "quickfindpattern.h"

const bool PersistentInfo::ForcePortable = true;

namespace {

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
            auto text = AnsiSgrParser::processDisplayLine( line, QColor(), QColor() ).text;
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

    const auto report = [ &]( const char* name, double us ) {
        const double usPerFrame = us / kFrames;
        std::cout << std::left << std::setw( 44 ) << name << std::right << std::setw( 12 )
                  << static_cast<int64_t>( us ) << " us" << std::setw( 10 ) << std::fixed
                  << std::setprecision( 2 ) << usPerFrame << " us/frame" << std::endl;
    };

    report( "Highlight recompute, all lines match", usAll );
    report( "Highlight recompute, no lines match", usNone );
}

} // namespace

int main( int argc, char* argv[] )
{
    QApplication app( argc, argv );
    logging::enableLogging();
    Configuration::getSynced();

    std::cout << "QuickFind baseline benchmark" << std::endl;
    std::cout << std::string( 80, '=' ) << std::endl;

    benchForwardScan();
    benchBackwardPathological();
    benchHighlightRecompute();

    return 0;
}
