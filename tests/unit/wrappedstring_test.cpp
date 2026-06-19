#include <catch2/catch.hpp>

#include <QStringView>

#include "wrappedstring.h"

namespace {

// ---------- Character-based wrapping (regression) ----------
// Uses existing WrappedString(QString, LineLength) constructor

TEST_CASE( "WrappedString character-based wrapping fits within column count" )
{
    WrappedString ws( QStringLiteral( "abcdefghijklmnopqrst" ), LineLength{ 10 } );
    REQUIRE( ws.wrappedLinesCount() == 2 );
    REQUIRE( ws.wrappedLine( 0 ).toString() == QStringLiteral( "abcdefghij" ) );
    REQUIRE( ws.wrappedLine( 1 ).toString() == QStringLiteral( "klmnopqrst" ) );
}

TEST_CASE( "WrappedString character-based wrapping at word boundary" )
{
    WrappedString ws( QStringLiteral( "hello world test" ), LineLength{ 8 } );
    REQUIRE( ws.wrappedLinesCount() >= 2 );
    REQUIRE( ws.wrappedLine( 0 ).toString() == QStringLiteral( "hello " ) );
}

TEST_CASE( "WrappedString character-based empty string" )
{
    WrappedString ws( QString{}, LineLength{ 10 } );
    REQUIRE( ws.wrappedLinesCount() == 1 );
    REQUIRE( ws.wrappedLine( 0 ).isEmpty() );
}

TEST_CASE( "WrappedString character-based short string needs no wrap" )
{
    WrappedString ws( QStringLiteral( "short" ), LineLength{ 10 } );
    REQUIRE( ws.wrappedLinesCount() == 1 );
    REQUIRE( ws.wrappedLine( 0 ).toString() == QStringLiteral( "short" ) );
}

// ---------- Pixel-based wrapping ----------
// Uses WrappedString(QString, int, TextWidthFn) constructor

// Fixed-width mock: each character is 10px wide
int fixedWidthFn( QStringView s )
{
    return static_cast<int>( s.size() ) * 10;
}

// Mixed-width mock: 'W' is 15px, others are 10px
int mixedWidthFn( QStringView s )
{
    int width = 0;
    for ( auto c : s ) {
        width += ( c == QChar( 'W' ) ) ? 15 : 10;
    }
    return width;
}

TEST_CASE( "WrappedString pixel wrapping wraps at pixel boundary" )
{
    WrappedString ws( QStringLiteral( "abcdefghij" ), 85, fixedWidthFn );
    REQUIRE( ws.wrappedLinesCount() == 2 );
    REQUIRE( ws.wrappedLine( 0 ).toString() == QStringLiteral( "abcdefgh" ) );
    REQUIRE( ws.wrappedLine( 1 ).toString() == QStringLiteral( "ij" ) );
}

TEST_CASE( "WrappedString pixel wrapping fills available width" )
{
    WrappedString ws( QStringLiteral( "aWcdef" ), 50, mixedWidthFn );
    REQUIRE( ws.wrappedLinesCount() == 2 );
    REQUIRE( ws.wrappedLine( 0 ).toString() == QStringLiteral( "aWcd" ) );
    REQUIRE( ws.wrappedLine( 1 ).toString() == QStringLiteral( "ef" ) );
}

TEST_CASE( "WrappedString pixel wrapping respects word boundary" )
{
    WrappedString ws( QStringLiteral( "hello world test" ), 70, fixedWidthFn );
    REQUIRE( ws.wrappedLinesCount() >= 2 );
    REQUIRE( ws.wrappedLine( 0 ).toString() == QStringLiteral( "hello " ) );
}

TEST_CASE( "WrappedString pixel wrapping hard-wraps long word" )
{
    WrappedString ws( QStringLiteral( "abcdefghij" ), 45, fixedWidthFn );
    REQUIRE( ws.wrappedLinesCount() >= 2 );
    REQUIRE( ws.wrappedLine( 0 ).toString() == QStringLiteral( "abcd" ) );
}

TEST_CASE( "WrappedString pixel wrapping single char per line" )
{
    WrappedString ws( QStringLiteral( "abc" ), 10, fixedWidthFn );
    REQUIRE( ws.wrappedLinesCount() == 3 );
    REQUIRE( ws.wrappedLine( 0 ).toString() == QStringLiteral( "a" ) );
    REQUIRE( ws.wrappedLine( 1 ).toString() == QStringLiteral( "b" ) );
    REQUIRE( ws.wrappedLine( 2 ).toString() == QStringLiteral( "c" ) );
}

TEST_CASE( "WrappedString pixel wrapping empty string" )
{
    WrappedString ws( QString{}, 100, fixedWidthFn );
    REQUIRE( ws.wrappedLinesCount() == 1 );
    REQUIRE( ws.wrappedLine( 0 ).isEmpty() );
}

TEST_CASE( "WrappedString pixel wrapping short string needs no wrap" )
{
    WrappedString ws( QStringLiteral( "short" ), 100, fixedWidthFn );
    REQUIRE( ws.wrappedLinesCount() == 1 );
    REQUIRE( ws.wrappedLine( 0 ).toString() == QStringLiteral( "short" ) );
}

TEST_CASE( "WrappedString pixel wrapping exact fit" )
{
    WrappedString ws( QStringLiteral( "abcdefgh" ), 80, fixedWidthFn );
    REQUIRE( ws.wrappedLinesCount() == 1 );
    REQUIRE( ws.wrappedLine( 0 ).toString() == QStringLiteral( "abcdefgh" ) );
}

TEST_CASE( "WrappedString pixel wrapping one pixel over causes wrap" )
{
    WrappedString ws( QStringLiteral( "abcdefghi" ), 89, fixedWidthFn );
    REQUIRE( ws.wrappedLinesCount() == 2 );
    REQUIRE( ws.wrappedLine( 0 ).toString() == QStringLiteral( "abcdefgh" ) );
    REQUIRE( ws.wrappedLine( 1 ).toString() == QStringLiteral( "i" ) );
}

TEST_CASE( "WrappedString pixel wrapping with mixed width fills more than char count" )
{
    WrappedString ws( QStringLiteral( "aWcdefghij" ), 50, mixedWidthFn );
    REQUIRE( ws.wrappedLine( 0 ).toString() == QStringLiteral( "aWcd" ) );
}

TEST_CASE( "WrappedString pixel wrapping word boundary earlier than pixel limit" )
{
    WrappedString ws( QStringLiteral( "abc def" ), 70, fixedWidthFn );
    REQUIRE( ws.wrappedLinesCount() == 1 );
    REQUIRE( ws.wrappedLine( 0 ).toString() == QStringLiteral( "abc def" ) );
}

TEST_CASE( "WrappedString pixel wrapping prefers word break over char break" )
{
    WrappedString ws( QStringLiteral( "hello world test" ), 90, fixedWidthFn );
    REQUIRE( ws.wrappedLinesCount() >= 2 );
    REQUIRE( ws.wrappedLine( 0 ).toString() == QStringLiteral( "hello " ) );
}

TEST_CASE( "WrappedString pixel wrapping no wrapped line exceeds available width" )
{
    WrappedString ws( QStringLiteral( "aWWbc defWWghi jklWW" ), 55, mixedWidthFn );

    for ( size_t i = 0; i < ws.wrappedLinesCount(); ++i ) {
        int linePx = mixedWidthFn( ws.wrappedLine( i ) );
        REQUIRE( linePx <= 55 );
    }
}

} // namespace
