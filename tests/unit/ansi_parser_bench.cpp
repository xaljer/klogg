/*
 * Copyright (C) 2024 Anton Filimonov and other contributors
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

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <QColor>
#include <QString>

#include "ansi_parser.h"

static const QColor kDefaultFg( 255, 255, 255 );
static const QColor kDefaultBg( 0, 0, 0 );

static const std::vector<QString> kPlainLines = {
    QStringLiteral( "2024-01-15 12:34:56.789 [INFO] [thread-5] Request processed successfully" ),
    QStringLiteral( "2024-01-15 12:34:56.890 [DEBUG] [thread-12] Cache miss for key user_3421" ),
    QStringLiteral(
        "2024-01-15 12:34:56.991 [WARN] [thread-3] Rate limit approached: 95/100" ),
    QStringLiteral(
        "2024-01-15 12:34:57.092 [ERROR] [thread-8] Database query took 2342ms" ),
    QStringLiteral(
        "2024-01-15 12:34:57.193 [INFO] [thread-1] Health check passed" ),
    QStringLiteral(
        "2024-01-15 12:34:57.294 [DEBUG] [thread-22] Configuration reloaded from disk" ),
    QStringLiteral(
        "2024-01-15 12:34:57.395 [FATAL] [thread-7] Out of memory in zone GenPermanent" ),
    QStringLiteral(
        "2024-01-15 12:34:57.496 [INFO] [thread-15] Batch processing: 1423 items/s" ),
};

static const std::vector<QString> kAnsiSimpleLines = {
    QStringLiteral(
        "\033[36m2024-01-15 12:34:56.789 [DEBUG] [thread-5] Request processed successfully\033[0m" ),
    QStringLiteral(
        "\033[32m2024-01-15 12:34:56.890 [INFO] [thread-12] Cache miss for key user_3421\033[0m" ),
    QStringLiteral(
        "\033[33m2024-01-15 12:34:56.991 [WARN] [thread-3] Rate limit approached: 95/100\033[0m" ),
    QStringLiteral(
        "\033[31m2024-01-15 12:34:57.092 [ERROR] [thread-8] Database query took 2342ms\033[0m" ),
    QStringLiteral(
        "\033[1;31m2024-01-15 12:34:57.395 [FATAL] [thread-7] Out of memory\033[0m" ),
};

static const std::vector<QString> kAnsiFullLines = {
    QStringLiteral(
        "\033[36m2024-01-15 12:34:56.789\033[0m \033[32m[INFO]\033[0m [thread-\033[1m5\033[0m] "
        "Request processed \033[92msuccessfully\033[0m" ),
    QStringLiteral(
        "\033[36m2024-01-15 12:34:56.890\033[0m \033[36m[DEBUG]\033[0m [thread-\033[1m12\033[0m] "
        "Cache \033[31mmiss\033[0m for key \033[33muser_3421\033[0m" ),
    QStringLiteral(
        "\033[36m2024-01-15 12:34:56.991\033[0m \033[33m[WARN]\033[0m [thread-\033[1m3\033[0m] "
        "Rate limit approached: \033[91m95/100\033[0m" ),
    QStringLiteral(
        "\033[36m2024-01-15 12:34:57.092\033[0m \033[1;31m[ERROR]\033[0m [thread-8] "
        "Database query took \033[38;5;196m2342ms\033[0m" ),
    QStringLiteral(
        "\033[36m2024-01-15 12:34:57.395\033[0m \033[1;31m[FATAL]\033[0m [thread-7] "
        "Out of memory in zone \033[38;2;255;128;0mGenPermanent\033[0m" ),
};

struct BenchResult {
    const char* name;
    int64_t totalLines;
    double totalUs;
    double nsPerLine;
    double linesPerMs;
};

static BenchResult bench( const char* name, const std::vector<QString>& lines, AnsiProcessing mode,
                          int rounds )
{
    std::mt19937 rng( 42 );
    std::uniform_int_distribution<size_t> dist( 0, lines.size() - 1 );

    std::vector<QString> testLines;
    testLines.reserve( static_cast<size_t>( rounds ) );
    for ( int i = 0; i < rounds; ++i ) {
        testLines.push_back( lines[ dist( rng ) ] );
    }

    const auto t0 = std::chrono::steady_clock::now();
    for ( const auto& line : testLines ) {
        AnsiSgrParser::parseLine( line, mode, kDefaultFg, kDefaultBg );
    }
    const auto t1 = std::chrono::steady_clock::now();

    const auto us = std::chrono::duration_cast<std::chrono::microseconds>( t1 - t0 ).count();

    return { name,          rounds,
             static_cast<double>( us ),        static_cast<double>( us ) * 1000.0 / rounds,
             static_cast<double>( rounds ) / static_cast<double>( us ) * 1000.0 };
}

static void printHeader()
{
    std::cout << std::left << std::setw( 40 ) << "Scenario" << std::right << std::setw( 10 )
              << "Lines" << std::setw( 12 ) << "Total(us)" << std::setw( 12 ) << "ns/line"
              << std::setw( 14 ) << "lines/ms" << std::endl;
    std::cout << std::string( 88, '-' ) << std::endl;
}

static void printResult( const BenchResult& r )
{
    std::cout << std::left << std::setw( 40 ) << r.name << std::right << std::setw( 10 )
              << r.totalLines << std::setw( 12 ) << static_cast<int64_t>( r.totalUs )
              << std::setw( 12 ) << static_cast<int64_t>( r.nsPerLine ) << std::setw( 14 )
              << std::fixed << std::setprecision( 1 ) << r.linesPerMs << std::endl;
}

int main()
{
    static const int kRounds = 100000;

    std::vector<BenchResult> results;

    results.push_back( bench( "None mode (fast path)", kPlainLines, AnsiProcessing::None,
                              kRounds ) );
    results.push_back(
        bench( "Plain lines, StripOnly", kPlainLines, AnsiProcessing::StripOnly, kRounds ) );
    results.push_back(
        bench( "Plain lines, RenderColors", kPlainLines, AnsiProcessing::RenderColors, kRounds ) );
    results.push_back( bench( "Simple ANSI, StripOnly", kAnsiSimpleLines,
                              AnsiProcessing::StripOnly, kRounds ) );
    results.push_back( bench( "Simple ANSI, RenderColors", kAnsiSimpleLines,
                              AnsiProcessing::RenderColors, kRounds ) );
    results.push_back(
        bench( "Full ANSI, StripOnly", kAnsiFullLines, AnsiProcessing::StripOnly, kRounds ) );
    results.push_back(
        bench( "Full ANSI, RenderColors", kAnsiFullLines, AnsiProcessing::RenderColors, kRounds ) );

    // Estimate per-frame cost (100 visible lines)
    const auto perFrameUs
        = []( const BenchResult& r ) { return r.nsPerLine * 100.0 / 1000.0; };

    printHeader();
    for ( const auto& r : results ) {
        printResult( r );
    }

    std::cout << std::endl;
    std::cout << "Estimated per-frame cost (100 visible lines):" << std::endl;
    std::cout << std::string( 60, '-' ) << std::endl;
    for ( const auto& r : results ) {
        std::cout << std::left << std::setw( 40 ) << r.name << std::right << std::setw( 10 )
                  << std::fixed << std::setprecision( 1 ) << perFrameUs( r ) << " us/frame"
                  << std::endl;
    }

    return 0;
}
