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

#include "layout.h"

// Golden tests for the chunk layout. The expectations transcribe what
// drawTextArea plus LineDrawer produced before the layout moved into core, so
// the column ranges, the row breaks and the colour choices are all pinned.
namespace {

using klogg::render::layoutLine;
using klogg::render::LineLayout;
using klogg::render::LineLayoutInput;

using klogg::Rgba;

// Columns are UTF-16 code units; the layout reports them as int64.
using Column = LineColumn::UnderlyingType;

constexpr Rgba lineFore{ 0x11, 0x22, 0x33, 0xff };
constexpr Rgba lineBack{ 0x44, 0x55, 0x66, 0xff };
constexpr Rgba matchFore{ 0x77, 0x88, 0x99, 0xff };
constexpr Rgba matchBack{ 0xaa, 0xbb, 0xcc, 0xff };

struct Run {
    Column start;
    Column length;
    bool startsRow;
    Rgba fore;
    Rgba back;

    bool operator==( const Run& other ) const = default;
};

LineLayoutInput makeInput( std::string_view line )
{
    LineLayoutInput input;
    input.line = line;
    input.foreColor = lineFore;
    input.backColor = lineBack;
    return input;
}

std::vector<Run> runs( const LineLayout& layout )
{
    std::vector<Run> result;
    for ( const auto& chunk : layout.chunks ) {
        result.push_back( Run{ chunk.start.get(), chunk.length.get(), chunk.startsRow,
                               chunk.foreColor, chunk.backColor } );
    }
    return result;
}

// Column ranges the line is wrapped into.
std::vector<std::pair<Column, Column>> parts( const LineLayout& layout )
{
    std::vector<std::pair<Column, Column>> result;
    for ( const auto& part : layout.parts ) {
        result.emplace_back( part.start.get(), part.length.get() );
    }
    return result;
}

// What a painter doing `text.substr( start, length )` per run produces: the text
// of each visual row. Only valid for inputs where one column is one byte.
std::vector<std::string> paintedRows( const LineLayout& layout, std::string_view text )
{
    std::vector<std::string> result{ std::string{} };
    for ( const auto& chunk : layout.chunks ) {
        if ( chunk.startsRow ) {
            result.emplace_back();
        }
        if ( chunk.length.get() == 0 ) {
            continue;
        }
        result.back().append( text.substr( static_cast<std::size_t>( chunk.start.get() ),
                                           static_cast<std::size_t>( chunk.length.get() ) ) );
    }
    return result;
}

// UTF-8 spelled out byte by byte so the test file stays ASCII-only.
constexpr std::string_view cjkWord = "\xe4\xb8\xad\xe6\x96\x87\xe6\xb5\x8b\xe8\xaf\x95";

TEST_CASE( "layout: a line without highlights is a single run" )
{
    auto input = makeInput( "hello world" );
    input.visibleColumns = LineLength{ 20 };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( parts( layout ) == std::vector<std::pair<Column, Column>>{ { 0, 11 } } );
    REQUIRE( runs( layout ) == std::vector<Run>{ Run{ 0, 11, false, lineFore, lineBack } } );
    REQUIRE( paintedRows( layout, "hello world" ) == std::vector<std::string>{ "hello world" } );
}

TEST_CASE( "layout: a line without highlights honours the horizontal scroll window" )
{
    // First visible column 5, four columns visible: the run covers columns 5 to
    // 9 and belongs to the row the painter is already in (startsRow is false).
    auto input = makeInput( "hello world" );
    input.firstColumn = LineColumn{ 5 };
    input.visibleColumns = LineLength{ 4 };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( runs( layout ) == std::vector<Run>{ Run{ 5, 5, false, lineFore, lineBack } } );
    REQUIRE( paintedRows( layout, "hello world" ) == std::vector<std::string>{ " worl" } );
}

TEST_CASE( "layout: highlights split the line into coloured runs" )
{
    // Two highlights with a gap before, between and after them: the line colour
    // fills those three gaps.
    auto input = makeInput( "abcdefghij" );
    input.visibleColumns = LineLength{ 10 };
    input.highlights = HighlightedMatchRanges{ klogg::vector<HighlightedMatch>{
        HighlightedMatch{ LineColumn{ 2 }, LineLength{ 3 }, matchFore, matchBack },
        HighlightedMatch{ LineColumn{ 7 }, LineLength{ 1 }, matchFore, matchBack } } };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( runs( layout )
             == std::vector<Run>{
                 Run{ 0, 2, false, lineFore, lineBack }, Run{ 2, 3, false, matchFore, matchBack },
                 Run{ 5, 2, false, lineFore, lineBack }, Run{ 7, 1, false, matchFore, matchBack },
                 Run{ 8, 2, false, lineFore, lineBack } } );
    REQUIRE( paintedRows( layout, "abcdefghij" ) == std::vector<std::string>{ "abcdefghij" } );
}

TEST_CASE( "layout: highlights are clamped to the visible window" )
{
    // The highlight starts before the window and is cut at its left edge; the
    // one past the right edge is dropped, so only the window is painted.
    auto input = makeInput( "abcdefghij" );
    input.firstColumn = LineColumn{ 4 };
    input.visibleColumns = LineLength{ 3 };
    input.highlights = HighlightedMatchRanges{ klogg::vector<HighlightedMatch>{
        HighlightedMatch{ LineColumn{ 0 }, LineLength{ 6 }, matchFore, matchBack },
        HighlightedMatch{ LineColumn{ 9 }, LineLength{ 1 }, matchFore, matchBack } } };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( runs( layout )
             == std::vector<Run>{ Run{ 4, 2, false, matchFore, matchBack },
                                  Run{ 6, 2, false, lineFore, lineBack } } );
    REQUIRE( paintedRows( layout, "abcdefghij" ) == std::vector<std::string>{ "efgh" } );
}

TEST_CASE( "layout: a highlight running past the end of the line is cut there" )
{
    // The highlight claims columns 3 to 9, the line has five columns: the run
    // covers columns 3 and 4, and the trailing line colour run is empty because
    // there is nothing left to paint (it only moves the cursor).
    auto input = makeInput( "abcde" );
    input.visibleColumns = LineLength{ 10 };
    input.highlights = HighlightedMatchRanges{ klogg::vector<HighlightedMatch>{
        HighlightedMatch{ LineColumn{ 3 }, LineLength{ 7 }, matchFore, matchBack } } };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( runs( layout )
             == std::vector<Run>{ Run{ 0, 3, false, lineFore, lineBack },
                                  Run{ 3, 2, false, matchFore, matchBack },
                                  Run{ 10, 0, false, lineFore, lineBack } } );
    REQUIRE( paintedRows( layout, "abcde" ) == std::vector<std::string>{ "abcde" } );
}

TEST_CASE( "layout: a highlight without a back colour keeps the line back colour" )
{
    // Alpha 0 is the unset sentinel: fore comes from the highlight, back falls
    // back to the caller's colour (abstractlogview.cpp:3005-3007).
    auto input = makeInput( "abcdef" );
    input.visibleColumns = LineLength{ 6 };
    input.highlights = HighlightedMatchRanges{ klogg::vector<HighlightedMatch>{
        HighlightedMatch{ LineColumn{ 1 }, LineLength{ 2 }, matchFore, Rgba{} } } };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( runs( layout )
             == std::vector<Run>{ Run{ 0, 1, false, lineFore, lineBack },
                                  Run{ 1, 2, false, matchFore, lineBack },
                                  Run{ 3, 3, false, lineFore, lineBack } } );
}

TEST_CASE( "layout: wrapping gives the line one run per row" )
{
    // "hello world test" wraps into "hello " / "world " / "test" at width 8
    // (test_wrap.cpp), and the whole line is painted in the line colours.
    auto input = makeInput( "hello world test" );
    input.wrapColumns = LineColumn{ 8 };
    input.visibleColumns = LineLength{ 8 };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( parts( layout )
             == std::vector<std::pair<Column, Column>>{ { 0, 6 }, { 6, 6 }, { 12, 4 } } );
    REQUIRE( runs( layout )
             == std::vector<Run>{ Run{ 0, 6, false, lineFore, lineBack },
                                  Run{ 6, 6, true, lineFore, lineBack },
                                  Run{ 12, 4, true, lineFore, lineBack } } );
    REQUIRE( paintedRows( layout, "hello world test" )
             == std::vector<std::string>{ "hello ", "world ", "test" } );
}

TEST_CASE( "layout: a chunk starting on a wrap boundary moves to the next row first" )
{
    // The second highlight starts exactly where the second part does: the range
    // walk ends the first part with an empty run, which is what makes the
    // painter move to the next row before the text. That empty run must survive
    // the move.
    auto input = makeInput( "abcdefghijklmnopqrst" );
    input.wrapColumns = LineColumn{ 10 };
    input.visibleColumns = LineLength{ 10 };
    input.highlights = HighlightedMatchRanges{ klogg::vector<HighlightedMatch>{
        HighlightedMatch{ LineColumn{ 0 }, LineLength{ 10 }, matchFore, matchBack },
        HighlightedMatch{ LineColumn{ 10 }, LineLength{ 3 }, matchFore, matchBack } } };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( parts( layout ) == std::vector<std::pair<Column, Column>>{ { 0, 10 }, { 10, 10 } } );
    REQUIRE( runs( layout )
             == std::vector<Run>{ Run{ 0, 10, false, matchFore, matchBack },
                                  Run{ 10, 0, false, matchFore, matchBack },
                                  Run{ 10, 3, true, matchFore, matchBack },
                                  Run{ 13, 7, false, lineFore, lineBack } } );
    REQUIRE( paintedRows( layout, "abcdefghijklmnopqrst" )
             == std::vector<std::string>{ "abcdefghij", "klmnopqrst" } );
}

TEST_CASE( "layout: a highlight crossing a wrap boundary is split at the row" )
{
    auto input = makeInput( "abcdefghijklmnopqrst" );
    input.wrapColumns = LineColumn{ 10 };
    input.visibleColumns = LineLength{ 10 };
    input.highlights = HighlightedMatchRanges{ klogg::vector<HighlightedMatch>{
        HighlightedMatch{ LineColumn{ 5 }, LineLength{ 10 }, matchFore, matchBack } } };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( runs( layout )
             == std::vector<Run>{ Run{ 0, 5, false, lineFore, lineBack },
                                  Run{ 5, 5, false, matchFore, matchBack },
                                  Run{ 10, 5, true, matchFore, matchBack },
                                  Run{ 15, 5, false, lineFore, lineBack } } );
    REQUIRE( paintedRows( layout, "abcdefghijklmnopqrst" )
             == std::vector<std::string>{ "abcdefghij", "klmnopqrst" } );
}

TEST_CASE( "layout: wrapping wins over the horizontal scroll window" )
{
    // firstColumn is what the caller passed before text wrap was switched on;
    // a wrapped line always starts at column 0 and spans the whole line.
    auto input = makeInput( "abcdefghijkl" );
    input.wrapColumns = LineColumn{ 5 };
    input.firstColumn = LineColumn{ 7 };
    input.visibleColumns = LineLength{ 3 };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( runs( layout )
             == std::vector<Run>{ Run{ 0, 5, false, lineFore, lineBack },
                                  Run{ 5, 5, true, lineFore, lineBack },
                                  Run{ 10, 2, true, lineFore, lineBack } } );
}

TEST_CASE( "layout: a non-positive wrap width means no wrapping" )
{
    // Same deviation as wrapLine: the ported loop would not terminate for a
    // width of zero, so core treats it as a single row (test_wrap.cpp).
    auto input = makeInput( "abcdefghij" );
    input.wrapColumns = LineColumn{ 0 };
    input.visibleColumns = LineLength{ 4 };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( parts( layout ) == std::vector<std::pair<Column, Column>>{ { 0, 10 } } );
    REQUIRE( runs( layout ) == std::vector<Run>{ Run{ 0, 5, false, lineFore, lineBack } } );
}

TEST_CASE( "layout: an empty line is one empty chunk on one row" )
{
    auto input = makeInput( "" );
    input.wrapColumns = LineColumn{ 8 };
    input.visibleColumns = LineLength{ 8 };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( parts( layout ) == std::vector<std::pair<Column, Column>>{ { 0, 0 } } );
    REQUIRE( runs( layout ) == std::vector<Run>{ Run{ 0, 0, false, lineFore, lineBack } } );
    REQUIRE( paintedRows( layout, "" ) == std::vector<std::string>{ "" } );
}

TEST_CASE( "layout: a line scrolled past its end paints nothing" )
{
    auto input = makeInput( "abc" );
    input.firstColumn = LineColumn{ 10 };
    input.visibleColumns = LineLength{ 5 };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( runs( layout ) == std::vector<Run>{ Run{ 10, 0, false, lineFore, lineBack } } );
    REQUIRE( paintedRows( layout, "abc" ) == std::vector<std::string>{ "" } );
}

TEST_CASE( "layout: columns are UTF-16 code units, not bytes" )
{
    // Four ideographs are three bytes each but one column each, so a width of
    // two gives two rows of two ideographs.
    auto input = makeInput( cjkWord );
    input.wrapColumns = LineColumn{ 2 };
    input.visibleColumns = LineLength{ 2 };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( parts( layout ) == std::vector<std::pair<Column, Column>>{ { 0, 2 }, { 2, 2 } } );
    REQUIRE( runs( layout )
             == std::vector<Run>{ Run{ 0, 2, false, lineFore, lineBack },
                                  Run{ 2, 2, true, lineFore, lineBack } } );
}

TEST_CASE( "layout: runs stay inside the part they start in and tile the rows" )
{
    const std::string text = "the quick brown fox jumps over the lazy dog";
    auto input = makeInput( text );
    input.wrapColumns = LineColumn{ 9 };
    input.visibleColumns = LineLength{ 9 };
    input.highlights = HighlightedMatchRanges{ klogg::vector<HighlightedMatch>{
        HighlightedMatch{ LineColumn{ 4 }, LineLength{ 28 }, matchFore, matchBack } } };

    const auto layout = layoutLine( std::move( input ) );

    std::size_t row = 0;
    Column column = 0;
    for ( const auto& chunk : layout.chunks ) {
        if ( chunk.startsRow ) {
            ++row;
            column = 0;
        }

        REQUIRE( row < layout.parts.size() );
        const auto partStart = layout.parts[ row ].start.get();
        const auto partLength = layout.parts[ row ].length.get();

        REQUIRE( chunk.start.get() == partStart + column );
        REQUIRE( chunk.length.get() <= partLength - column );
        column += chunk.length.get();
    }

    // Every row is painted exactly up to its end.
    REQUIRE( column == layout.parts.back().length.get() );
}

TEST_CASE( "layout: a wrapped line is painted in as many rows as it has parts" )
{
    auto input = makeInput( "abcdefghijklmnopqrst" );
    input.wrapColumns = LineColumn{ 7 };
    input.visibleColumns = LineLength{ 7 };

    const auto layout = layoutLine( std::move( input ) );

    REQUIRE( layout.parts.size() == 3 );
    REQUIRE( paintedRows( layout, "abcdefghijklmnopqrst" )
             == std::vector<std::string>{ "abcdefg", "hijklmn", "opqrst" } );
}

} // namespace
