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

#include <catch2/catch.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "wrap.h"

// Golden tests for the Qt-free column wrapper: columns are UTF-16 code units,
// the unit HighlighterSet reports match positions in, and the last space inside
// the wrap-width region ends a part.
namespace {

using klogg::render::wrapLine;

// Expected (start column, length in columns) pairs; the 0-based part index
// reported by wrapLine is checked here for every part.
std::vector<std::pair<std::size_t, std::size_t>> wrapPoints( std::string_view text,
                                                             int wrapColumns )
{
    std::vector<std::pair<std::size_t, std::size_t>> points;
    std::size_t index = 0;
    for ( const auto& part : wrapLine( text, LineColumn{ wrapColumns } ) ) {
        REQUIRE( part.line.get() == index );
        points.emplace_back( static_cast<std::size_t>( part.start.get() ),
                             static_cast<std::size_t>( part.length.get() ) );
        ++index;
    }
    return points;
}

// Same as wrapPoints, but materialised as text. Only valid for inputs where one
// column is one byte (ASCII).
std::vector<std::string> wrapText( std::string_view asciiText, int wrapColumns )
{
    std::vector<std::string> texts;
    for ( const auto& point : wrapPoints( asciiText, wrapColumns ) ) {
        texts.emplace_back( asciiText.substr( point.first, point.second ) );
    }
    return texts;
}

// Parts must tile the line: contiguous, covering every column, each within the
// requested width and never empty.
void requireTiling( std::string_view text, int wrapColumns, std::size_t totalColumns )
{
    std::size_t expectedStart = 0;
    const auto points = wrapPoints( text, wrapColumns );

    REQUIRE( !points.empty() );
    for ( const auto& point : points ) {
        REQUIRE( point.first == expectedStart );
        REQUIRE( point.second > 0 );
        REQUIRE( point.second <= static_cast<std::size_t>( wrapColumns ) );
        expectedStart += point.second;
    }
    REQUIRE( expectedStart == totalColumns );
}

// UTF-8 spelled out byte by byte so the test file stays ASCII-only and the
// expectations do not depend on the compiler's source charset setting.
constexpr std::string_view cjkIdeograph = "\xe4\xb8\xad";        // U+4E2D
constexpr std::string_view cjkSecondIdeograph = "\xe6\x96\x87";  // U+6587
constexpr std::string_view cjkWord = "\xe6\xb5\x8b\xe8\xaf\x95"; // U+6D4B U+8BD5
constexpr std::string_view cjkBlock
    = "\xe4\xb8\xad\xe6\x96\x87\xe6\xb5\x8b\xe8\xaf\x95"; // 4 ideographs
constexpr std::string_view cjkSpace = "\xe3\x80\x80";     // U+3000 ideographic space
constexpr std::string_view noBreakSpace = "\xc2\xa0";     // U+00A0 NBSP
constexpr std::string_view emoji = "\xf0\x9f\x99\x82";    // U+1F642, two UTF-16 units

std::string join( std::string_view a, std::string_view b, std::string_view c = {},
                  std::string_view d = {}, std::string_view e = {} )
{
    std::string result;
    result.reserve( a.size() + b.size() + c.size() + d.size() + e.size() );
    result.append( a ).append( b ).append( c ).append( d ).append( e );
    return result;
}

TEST_CASE( "wrap: empty line yields one empty part" )
{
    // An empty line yields a single empty part.
    REQUIRE( wrapPoints( "", 10 ) == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 0 } } );
}

TEST_CASE( "wrap: line shorter than the wrap width is a single part" )
{
    // The wrap loop only runs while the remainder is wider than the wrap width,
    // so everything is emitted as the tail.
    REQUIRE( wrapText( "short", 10 ) == std::vector<std::string>{ "short" } );
    REQUIRE( wrapText( "ab cd", 10 ) == std::vector<std::string>{ "ab cd" } );
}

TEST_CASE( "wrap: line exactly as wide as the wrap width is a single part" )
{
    // A strict "wider than" test: an exactly full line is never followed by an
    // empty part.
    REQUIRE( wrapPoints( "abcdefgh", 8 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 8 } } );
    // Four ideographs are four columns, so 4 columns is an exact fit.
    REQUIRE( wrapPoints( cjkBlock, 4 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 4 } } );
}

TEST_CASE( "wrap: one column over the wrap width spills into a tail part" )
{
    REQUIRE( wrapText( "abcdefghi", 8 ) == std::vector<std::string>{ "abcdefgh", "i" } );
}

TEST_CASE( "wrap: no space means a hard cut at the wrap width" )
{
    // A region without a space is cut hard at the wrap width.
    REQUIRE( wrapText( "abcdefghijklmnopqrst", 10 )
             == std::vector<std::string>{ "abcdefghij", "klmnopqrst" } );
    REQUIRE( wrapText( "abcdefghijkl", 4 ) == std::vector<std::string>{ "abcd", "efgh", "ijkl" } );
}

TEST_CASE( "wrap: falls back to the last space and that space ends the part" )
{
    // The last space of the region ends the part: the cut lands one past that
    // space, so the space stays with the part it ends.
    REQUIRE( wrapText( "hello world test", 8 )
             == std::vector<std::string>{ "hello ", "world ", "test" } );
    REQUIRE( wrapText( "aaaa bbbb cccc dddd eeee", 9 )
             == std::vector<std::string>{ "aaaa ", "bbbb ", "cccc ", "dddd eeee" } );
}

TEST_CASE( "wrap: a space on the last column of the region ends the part" )
{
    // The cut can reach the full wrap width; the part is then as wide as the
    // region and the next part starts past the space.
    REQUIRE( wrapText( "abcdefg hij", 8 ) == std::vector<std::string>{ "abcdefg ", "hij" } );
}

TEST_CASE( "wrap: a tail that fits is never split further" )
{
    // Once the remainder fits the wrap width the loop exits and the rest is
    // emitted as one part, whatever spaces it contains.
    REQUIRE( wrapText( "abcd efgh ij", 8 ) == std::vector<std::string>{ "abcd ", "efgh ij" } );
}

TEST_CASE( "wrap: whitespace only line" )
{
    // Spaces are ordinary columns here: the region is cut after its last space.
    REQUIRE( wrapText( "   ", 2 ) == std::vector<std::string>{ "  ", " " } );
}

TEST_CASE( "wrap: single column width" )
{
    REQUIRE( wrapText( "abc", 1 ) == std::vector<std::string>{ "a", "b", "c" } );
}

TEST_CASE( "wrap: tab is a break point like any other space" )
{
    // A literal tab is a space for the break rule (as QChar::isSpace() says).
    // The paint path expands tabs before wrapping, this keeps the predicate
    // honest.
    REQUIRE( wrapPoints( "ab\tcd", 2 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 2 }, { 2, 1 }, { 3, 2 } } );
}

TEST_CASE( "wrap: text with an expanded tab" )
{
    // What the view actually wraps: lines already expanded by untabify.
    REQUIRE( wrapText( "ab      cd efgh", 8 )
             == std::vector<std::string>{ "ab      ", "cd efgh" } );
}

TEST_CASE( "wrap: CJK text counts UTF-16 code units, not display columns" )
{
    // One ideograph is one column (one UTF-16 code unit) even though a terminal
    // shows it two cells wide; the width model is the one HighlighterSet uses.
    const auto cjk = join( cjkIdeograph, cjkSecondIdeograph, cjkWord );
    REQUIRE( wrapPoints( cjk, 2 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 2 }, { 2, 2 } } );
    REQUIRE( wrapPoints( cjk, 3 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 3 }, { 3, 1 } } );
}

TEST_CASE( "wrap: non-BMP characters take two columns" )
{
    const auto mixed = join( "ab", emoji, "cd" );
    REQUIRE( wrapPoints( mixed, 2 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 2 }, { 2, 2 }, { 4, 2 } } );

    const auto threeEmoji = join( emoji, emoji, emoji );
    REQUIRE( wrapPoints( threeEmoji, 2 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 2 }, { 2, 2 }, { 4, 2 } } );
}

TEST_CASE( "wrap: a region can split a surrogate pair" )
{
    // The ported wrapper cuts by code unit, so a width that lands inside a
    // surrogate pair splits it - the same thing QStringView::left() did.
    const auto mixed = join( "ab", emoji, "cd" );
    REQUIRE( wrapPoints( mixed, 3 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 3 }, { 3, 3 } } );

    REQUIRE( wrapPoints( emoji, 1 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 1 }, { 1, 1 } } );
}

TEST_CASE( "wrap: Unicode spaces break the line" )
{
    // U+3000 and U+00A0 are both in the QChar::isSpace set, each takes one
    // column, and a region holding only the space ends right after it.
    const auto withIdeographicSpace = join( "ab", cjkSpace, "cd" );
    REQUIRE( wrapPoints( withIdeographicSpace, 2 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 2 }, { 2, 1 }, { 3, 2 } } );

    const auto withNoBreakSpace = join( "ab", noBreakSpace, "cd" );
    REQUIRE( wrapPoints( withNoBreakSpace, 2 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 2 }, { 2, 1 }, { 3, 2 } } );
}

TEST_CASE( "wrap: CJK mixed with ASCII words" )
{
    const auto mixed = join( cjkIdeograph, cjkSecondIdeograph, " ", cjkWord, " ab" );
    REQUIRE( wrapPoints( mixed, 3 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 3 }, { 3, 3 }, { 6, 2 } } );
}

TEST_CASE( "wrap: mixed ASCII, CJK and emoji line" )
{
    const auto mixed = join( join( "log ", cjkIdeograph, cjkSecondIdeograph, " " ),
                             join( emoji, " mixed words", " here" ) );
    REQUIRE( wrapPoints( mixed, 7 )
             == std::vector<std::pair<std::size_t, std::size_t>>{
                 { 0, 7 }, { 7, 3 }, { 10, 6 }, { 16, 6 }, { 22, 4 } } );
}

TEST_CASE( "wrap: malformed UTF-8 keeps a stable column count" )
{
    // Display text is decoded before wrapping, so this is a defensive path: a
    // line simdutf rejects falls back to one replacement character per
    // malformed byte instead of collapsing into a single empty part.
    const std::string loneInvalidBytes( "\xff\xfe", 2 );
    REQUIRE( wrapPoints( loneInvalidBytes, 10 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 2 } } );

    const std::string truncatedSequence( "\xe4\xb8", 2 );
    REQUIRE( wrapPoints( truncatedSequence, 10 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 2 } } );

    // Same width, so the invalid prefix is visible in the break points.
    const std::string validAfterInvalid = loneInvalidBytes + "ok";
    REQUIRE( wrapPoints( validAfterInvalid, 1 )
             == std::vector<std::pair<std::size_t, std::size_t>>{
                 { 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1 } } );
}

TEST_CASE( "wrap: non-positive width disables wrapping" )
{
    // Deliberate deviation: the ported loop would not terminate for a width of
    // zero, so core treats it as "no wrapping".
    REQUIRE( wrapPoints( "abcdef", 0 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 6 } } );
    REQUIRE( wrapPoints( "abcdef", -3 )
             == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 6 } } );

    const auto cjk = join( cjkIdeograph, cjkSecondIdeograph, cjkWord );
    REQUIRE( wrapPoints( cjk, 0 ) == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 4 } } );
    REQUIRE( wrapPoints( "", 0 ) == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 0 } } );
}

TEST_CASE( "wrap: parts always tile the line" )
{
    requireTiling( "hello world test", 8, 16 );
    requireTiling( "abcdefghijklmnopqrst", 10, 20 );
    requireTiling( "ab\tcd", 2, 5 );
    requireTiling( "   ", 2, 3 );
    requireTiling( join( cjkBlock, " ", emoji ), 3, 7 );
    requireTiling( std::string( "\xff\xfe" ) + "ab", 1, 4 );
}

} // namespace
