/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

#include <catch2/catch.hpp>

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTextStream>
#include <QUuid>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>

#include "klogg/api.h"

using namespace klogg::api;

namespace {

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
    file.path = QString( "%1/klogg_api_scrollmap_test_%2.log" )
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

constexpr auto IntRangeMax = std::numeric_limits<int>::max();

// Same scale the session uses, so the compressed branch (unreachable through a
// real file: it needs more lines than an int can address) can be checked
int64_t compressedPos( int64_t line, double multiplier )
{
    return std::llround( static_cast<double>( line ) * multiplier );
}

int64_t compressedLine( int64_t scrollPos, double multiplier )
{
    return std::llround( static_cast<double>( scrollPos ) / multiplier );
}

} // namespace

TEST_CASE( "scroll mapping keeps a file that fits the scroll range one to one",
           "[api][scrollmap]" )
{
    const auto file = writeTempLogFile( { "one", "two", "three" } );

    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );
    REQUIRE( opened->total_lines == 3_lcount );

    const auto multiplier = session->scrollMultiplier( opened->id );
    REQUIRE( multiplier.has_value() );
    REQUIRE( *multiplier == 1.0 );

    for ( LineNumber::UnderlyingType line = 0; line < 3; ++line ) {
        const auto scrollPos = session->lineToScrollPos( opened->id, LineNumber{ line } );
        REQUIRE( scrollPos.has_value() );
        REQUIRE( *scrollPos == static_cast<int64_t>( line ) );

        const auto back = session->scrollPosToLine( opened->id, *scrollPos );
        REQUIRE( back.has_value() );
        REQUIRE( *back == LineNumber{ line } );
    }
}

TEST_CASE( "scroll mapping reports an empty file as uncompressed", "[api][scrollmap]" )
{
    const auto file = writeTempLogFile( {} );

    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );
    REQUIRE( opened->total_lines == 0_lcount );

    // Nothing to compress, and a zero factor would collapse the mapping
    const auto multiplier = session->scrollMultiplier( opened->id );
    REQUIRE( multiplier.has_value() );
    REQUIRE( *multiplier == 1.0 );

    const auto line = session->scrollPosToLine( opened->id, 0 );
    REQUIRE( line.has_value() );
    REQUIRE( *line == 0_lnum );

    const auto scrollPos = session->lineToScrollPos( opened->id, 0_lnum );
    REQUIRE( scrollPos.has_value() );
    REQUIRE( *scrollPos == 0 );
}

TEST_CASE( "scroll mapping stays linear past the end of the file", "[api][scrollmap]" )
{
    const auto file = writeTempLogFile( { "one", "two", "three" } );

    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );

    // No clamping to the file either way: a scroll bar holds positions of its
    // own range, and the widget clamps whatever value it is given
    const auto pastEndLine = session->lineToScrollPos( opened->id, 10_lnum );
    REQUIRE( pastEndLine.has_value() );
    REQUIRE( *pastEndLine == 10 );

    const auto pastEndPos = session->scrollPosToLine( opened->id, 7 );
    REQUIRE( pastEndPos.has_value() );
    REQUIRE( *pastEndPos == 7_lnum );
}

TEST_CASE( "scroll mapping reads a negative position as the first line", "[api][scrollmap]" )
{
    const auto file = writeTempLogFile( { "one", "two", "three" } );

    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );

    const auto line = session->scrollPosToLine( opened->id, -5 );
    REQUIRE( line.has_value() );
    REQUIRE( *line == 0_lnum );
}

TEST_CASE( "scroll mapping reports an unknown file", "[api][scrollmap]" )
{
    auto session = createSession();

    const auto multiplier = session->scrollMultiplier( 12345 );
    REQUIRE( !multiplier.has_value() );
    REQUIRE( multiplier.error().code == ErrorCode::FileNotOpen );

    const auto line = session->scrollPosToLine( 12345, 0 );
    REQUIRE( !line.has_value() );
    REQUIRE( line.error().code == ErrorCode::FileNotOpen );

    const auto scrollPos = session->lineToScrollPos( 12345, 0_lnum );
    REQUIRE( !scrollPos.has_value() );
    REQUIRE( scrollPos.error().code == ErrorCode::FileNotOpen );
}

TEST_CASE( "the compression factor starts past the int range", "[api][scrollmap]" )
{
    REQUIRE( scrollMultiplierForLines( 0_lcount ) == 1.0 );
    REQUIRE( scrollMultiplierForLines( 1_lcount ) == 1.0 );
    REQUIRE( scrollMultiplierForLines( 1000_lcount ) == 1.0 );
    REQUIRE( scrollMultiplierForLines( LinesCount{ IntRangeMax } ) == 1.0 );

    // One line past the int range and the factor is int_max / lines
    const auto deep = LinesCount{ static_cast<uint64_t>( IntRangeMax ) + 1 };
    const auto expected = static_cast<double>( IntRangeMax ) / static_cast<double>( deep.get() );
    REQUIRE( scrollMultiplierForLines( deep ) == expected );
    REQUIRE( expected < 1.0 );

    // The factor shrinks as the file grows, and never reaches zero
    const auto deepest = LinesCount{ 2ULL * static_cast<uint64_t>( IntRangeMax ) };
    const auto deepestFactor = scrollMultiplierForLines( deepest );
    REQUIRE( deepestFactor < expected );
    REQUIRE( deepestFactor > 0.0 );
}

TEST_CASE( "a compressed mapping keeps the last line inside the int scroll range",
           "[api][scrollmap]" )
{
    // Twice the depth a scroll bar can address; no file this big can be
    // indexed in a test, so the factor carries the contract on its own
    const auto lines = LinesCount{ 2ULL * static_cast<uint64_t>( IntRangeMax ) };
    const auto multiplier = scrollMultiplierForLines( lines );
    REQUIRE( multiplier < 1.0 );

    const auto lastLine = static_cast<int64_t>( lines.get() - 1 );
    REQUIRE( compressedPos( lastLine, multiplier ) <= static_cast<int64_t>( IntRangeMax ) );

    // Positions resolve to within one line, which is what a compressed scroll
    // position can promise; the mapping stays monotonic
    int64_t previousPos = -1;
    for ( const auto line : { int64_t{ 0 }, int64_t{ 1 }, int64_t{ 1000000 }, lastLine } ) {
        const auto scrollPos = compressedPos( line, multiplier );
        REQUIRE( scrollPos >= previousPos );
        previousPos = scrollPos;
        REQUIRE( std::abs( compressedLine( scrollPos, multiplier ) - line ) <= 1 );
    }
}
