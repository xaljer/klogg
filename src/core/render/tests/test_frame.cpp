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

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "frame.h"

// Tests for the viewport composer: the steps the paint loop used to run per
// line (tab expansion, ANSI, highlights, wrapping and chunk layout) chained for
// a whole frame. The individual steps have their own golden tests; these pin
// the order they run in, the colours they hand each other and the per-line data
// the consumer paints from.
namespace {

using klogg::render::composeFrame;
using klogg::render::Frame;
using klogg::render::FrameInput;
using klogg::render::FrameLine;
using klogg::render::QuickHighlighterOptions;
using klogg::render::SelectionPortion;

using klogg::Rgba;

const Rgba PaletteText{ 0x11, 0x22, 0x33, 0xff };
const Rgba PaletteBase{ 0xaa, 0xbb, 0xcc, 0xff };
const Rgba DimmedText{ 0x55, 0x55, 0x55, 0xff };
const Rgba SelectionText{ 0xee, 0xee, 0xee, 0xff };
const Rgba SelectionBack{ 0x00, 0x00, 0x88, 0xff };
const Rgba RuleFore{ 0xff, 0x00, 0x00, 0xff };
const Rgba RuleBack{ 0x00, 0x88, 0x00, 0xff };
const Rgba QuickFindBack{ 0xff, 0xee, 0x00, 0xff };

FrameInput baseInput( std::vector<std::string> lines )
{
    FrameInput input;
    input.lines = std::move( lines );
    input.firstLine = 0_lnum;
    input.firstDisplayLineNumber = 1_lnum;
    input.searchStartIndex = 0_lnum;
    input.searchEndIndex = 100_lnum;
    input.visibleColumns = 40_length;
    input.palette = { PaletteText, PaletteBase, DimmedText, SelectionText, SelectionBack };
    return input;
}

Highlighter wordRule( const std::string& pattern, const Rgba& fore, const Rgba& back = Rgba{} )
{
    return Highlighter{ pattern, false, true, fore, back };
}

Highlighter lineRule( const std::string& pattern, const Rgba& fore, const Rgba& back = Rgba{} )
{
    return Highlighter{ pattern, false, false, fore, back };
}

HighlighterSet ruleSet( const klogg::vector<Highlighter>& rules )
{
    auto set = HighlighterSet::createNewSet( "frame test" );
    set.setHighlighters( rules );
    return set;
}

// "start:size:fore:back[:row]" per chunk, in paint order
std::vector<std::string> chunkList( const FrameLine& line )
{
    std::vector<std::string> result;
    for ( const auto& chunk : line.chunks ) {
        result.push_back( std::to_string( type_safe::get( chunk.start ) ) + ":"
                          + std::to_string( type_safe::get( chunk.length ) ) + ":"
                          + chunk.foreColor.toHex() + ":" + chunk.backColor.toHex()
                          + ( chunk.startsRow ? ":row" : "" ) );
    }
    return result;
}

// "start:length" per wrap part
std::vector<std::string> partList( const FrameLine& line )
{
    std::vector<std::string> result;
    for ( const auto& part : line.parts ) {
        result.push_back( std::to_string( type_safe::get( part.start ) ) + ":"
                          + std::to_string( type_safe::get( part.length ) ) );
    }
    return result;
}

} // namespace

TEST_CASE( "frame: a plain line is laid out in the palette colours", "[render][frame]" )
{
    const auto frame = composeFrame( baseInput( { "hello world" } ) );

    REQUIRE( frame.lines.size() == 1 );

    const auto& line = frame.lines.front();
    REQUIRE( line.displayText == "hello world" );
    REQUIRE( line.lineNumber == 0_lnum );
    REQUIRE( line.displayLineNumber == 1_lnum );
    REQUIRE( line.flagBits == 0 );
    REQUIRE( line.foreColor == PaletteText );
    REQUIRE( line.backColor == PaletteBase );
    REQUIRE( partList( line ) == std::vector<std::string>{ "0:11" } );
    // The run is clamped to the line even though the composition asks for the
    // whole visible window
    REQUIRE( chunkList( line ) == std::vector<std::string>{ "0:11:#ff112233:#ffaabbcc" } );
}

TEST_CASE( "frame: tabs are expanded and ANSI sequences stripped", "[render][frame]" )
{
    auto input = baseInput( { "a\tb", "plain \x1b[31mred\x1b[0m tail" } );
    input.ansiProcessing = AnsiProcessing::RenderColors;

    const auto frame = composeFrame( std::move( input ) );

    REQUIRE( frame.lines.size() == 2 );
    REQUIRE( frame.lines[ 0 ].displayText == "a       b" );
    REQUIRE( frame.lines[ 1 ].displayText == "plain red tail" );
}

TEST_CASE( "frame: ANSI colour offsets follow tab expansion", "[render][frame]" )
{
    // Tabs are expanded before the ANSI parser runs, so the coloured run has to
    // start at the expanded column, not at the offset of the escape sequence.
    auto input = baseInput( { "a\t\033[31mred\033[0m" } );
    input.ansiProcessing = AnsiProcessing::RenderColors;

    const auto frame = composeFrame( std::move( input ) );

    REQUIRE( frame.lines.size() == 1 );
    REQUIRE( frame.lines[ 0 ].displayText == "a       red" );

    const Rgba ansiRed{ 0xcd, 0x00, 0x00, 0xff };
    const auto& chunks = frame.lines[ 0 ].chunks;
    const auto redChunk
        = std::find_if( chunks.begin(), chunks.end(),
                        [ &ansiRed ]( const auto& chunk ) { return chunk.foreColor == ansiRed; } );

    REQUIRE( redChunk != chunks.end() );
    REQUIRE( type_safe::get( redChunk->start ) == 8 );
    REQUIRE( type_safe::get( redChunk->length ) == 3 );
}

TEST_CASE( "frame: numbers and gutter flags follow the frame position", "[render][frame]" )
{
    auto input = baseInput( { "one", "two", "three" } );
    input.firstLine = 10_lnum;
    input.firstDisplayLineNumber = 11_lnum;
    input.lineFlags = { 1, 2, 4 };

    const auto frame = composeFrame( std::move( input ) );

    REQUIRE( frame.lines.size() == 3 );
    for ( size_t i = 0; i < frame.lines.size(); ++i ) {
        const auto index = static_cast<LineNumber::UnderlyingType>( i );
        REQUIRE( frame.lines[ i ].lineNumber == LineNumber{ 10 + index } );
        REQUIRE( frame.lines[ i ].displayLineNumber == LineNumber{ 11 + index } );
        REQUIRE( frame.lines[ i ].flagBits == static_cast<uint8_t>( 1u << i ) );
    }
}

TEST_CASE( "frame: an absent flag array leaves every line plain", "[render][frame]" )
{
    const auto frame = composeFrame( baseInput( { "one", "two" } ) );

    REQUIRE( frame.lines.size() == 2 );
    REQUIRE( frame.lines[ 0 ].flagBits == 0 );
    REQUIRE( frame.lines[ 1 ].flagBits == 0 );
}

TEST_CASE( "frame: viewLines carries non-contiguous line numbers through the frame",
           "[render][frame]" )
{
    auto input = baseInput( { "ten", "twenty", "thirty" } );
    input.firstLine = 100_lnum;
    input.firstDisplayLineNumber = 1_lnum;
    input.viewLines = { 10_lnum, 20_lnum, 30_lnum };
    // Only "twenty" is in the search range: the range test must use its view
    // line, not the frame position
    input.searchStartIndex = 20_lnum;
    input.searchEndIndex = 21_lnum;

    const auto frame = composeFrame( std::move( input ) );

    REQUIRE( frame.lines.size() == 3 );
    REQUIRE( frame.lines[ 0 ].lineNumber == 10_lnum );
    REQUIRE( frame.lines[ 1 ].lineNumber == 20_lnum );
    REQUIRE( frame.lines[ 2 ].lineNumber == 30_lnum );
    // Display numbers still follow the frame position
    REQUIRE( frame.lines[ 0 ].displayLineNumber == 1_lnum );
    REQUIRE( frame.lines[ 1 ].displayLineNumber == 2_lnum );
    REQUIRE( frame.lines[ 2 ].displayLineNumber == 3_lnum );
    // The first and the last lines are dimmed, the middle one is in range
    REQUIRE( frame.lines[ 0 ].foreColor == DimmedText );
    REQUIRE( frame.lines[ 1 ].foreColor == PaletteText );
    REQUIRE( frame.lines[ 2 ].foreColor == DimmedText );
}

TEST_CASE( "frame: a short viewLines vector falls back to firstLine numbering", "[render][frame]" )
{
    auto input = baseInput( { "one", "two", "three" } );
    input.firstLine = 5_lnum;
    input.viewLines = { 40_lnum };

    const auto frame = composeFrame( std::move( input ) );

    REQUIRE( frame.lines[ 0 ].lineNumber == 40_lnum );
    REQUIRE( frame.lines[ 1 ].lineNumber == 6_lnum );
    REQUIRE( frame.lines[ 2 ].lineNumber == 7_lnum );
}

TEST_CASE( "frame: wrapping splits the lines and keeps the search range in view",
           "[render][frame]" )
{
    auto input = baseInput( { "0123456789", "abcdefghij" } );
    input.wrapColumns = 4_lcol;
    input.visibleColumns = 4_length;
    input.searchStartIndex = 1_lnum;
    input.searchEndIndex = 2_lnum;

    const auto frame = composeFrame( std::move( input ) );

    REQUIRE( partList( frame.lines[ 0 ] ) == std::vector<std::string>{ "0:4", "4:4", "8:2" } );
    REQUIRE( partList( frame.lines[ 1 ] ) == std::vector<std::string>{ "0:4", "4:4", "8:2" } );

    // Outside the search range the text is dimmed
    REQUIRE( frame.lines[ 0 ].foreColor == DimmedText );
    REQUIRE( frame.lines[ 1 ].foreColor == PaletteText );
    REQUIRE( frame.lines[ 1 ].backColor == PaletteBase );
}

TEST_CASE( "frame: a whole line rule paints the row background", "[render][frame]" )
{
    SECTION( "with a back colour" )
    {
        const auto set = ruleSet( { lineRule( "ERROR", RuleFore, RuleBack ) } );

        auto input = baseInput( { "ERROR: disk failure" } );
        input.highlighterSet = &set;

        const auto frame = composeFrame( std::move( input ) );

        // The row background follows the rule, so its colour fills the whole
        // width of the line and not only the match run
        REQUIRE( frame.lines.front().foreColor == RuleFore );
        REQUIRE( frame.lines.front().backColor == RuleBack );
        // The trailing zero length run is the layout's row cursor hint
        REQUIRE(
            chunkList( frame.lines.front() )
            == std::vector<std::string>{ "0:19:#ffff0000:#ff008800", "19:0:#ffff0000:#ff008800" } );
    }

    SECTION( "without a back colour the palette keeps the row" )
    {
        const auto set = ruleSet( { lineRule( "ERROR", RuleFore ) } );

        auto input = baseInput( { "ERROR: disk failure" } );
        input.highlighterSet = &set;

        const auto frame = composeFrame( std::move( input ) );

        REQUIRE( frame.lines.front().foreColor == RuleFore );
        REQUIRE( frame.lines.front().backColor == PaletteBase );
    }
}

TEST_CASE( "frame: the rules do not run outside the search range", "[render][frame]" )
{
    const auto set = ruleSet( { wordRule( "ERROR", RuleFore, RuleBack ) } );

    auto input = baseInput( { "ERROR: disk failure" } );
    input.searchStartIndex = 5_lnum;
    input.searchEndIndex = 10_lnum;
    input.highlighterSet = &set;

    const auto frame = composeFrame( std::move( input ) );

    REQUIRE( frame.lines.front().foreColor == DimmedText );
    REQUIRE( chunkList( frame.lines.front() )
             == std::vector<std::string>{ "0:19:#ff555555:#ffaabbcc" } );
}

TEST_CASE( "frame: a whole line selection drops the highlights", "[render][frame]" )
{
    const auto set = ruleSet( { wordRule( "ERROR", RuleFore, RuleBack ) } );

    auto input = baseInput( { "ERROR: disk failure", "selection below" } );
    input.highlighterSet = &set;

    SelectionPortion selectedLine;
    selectedLine.lineIndex = 0;
    selectedLine.wholeLine = true;
    input.selectionPortions = { selectedLine };

    const auto frame = composeFrame( std::move( input ) );

    // The selected row takes the selection colours and none of the rules
    REQUIRE( frame.lines[ 0 ].foreColor == SelectionText );
    REQUIRE( frame.lines[ 0 ].backColor == SelectionBack );
    REQUIRE( chunkList( frame.lines[ 0 ] )
             == std::vector<std::string>{ "0:19:#ffeeeeee:#ff000088" } );

    // The line below is composed as usual
    REQUIRE( frame.lines[ 1 ].foreColor == PaletteText );
    REQUIRE( chunkList( frame.lines[ 1 ] ).empty() == false );
}

TEST_CASE( "frame: a selected portion is painted over the highlights", "[render][frame]" )
{
    const auto set = ruleSet( { wordRule( "ERROR", RuleFore ) } );

    auto input = baseInput( { "ERROR: disk failure" } );
    input.highlighterSet = &set;

    SelectionPortion portion;
    portion.lineIndex = 0;
    portion.startColumn = 10_lcol;
    portion.length = 5_length;
    input.selectionPortions = { portion };

    const auto frame = composeFrame( std::move( input ) );

    REQUIRE( chunkList( frame.lines.front() )
             == std::vector<std::string>{ "0:5:#ffff0000:#ffaabbcc", "5:5:#ff112233:#ffaabbcc",
                                          "10:5:#ffeeeeee:#ff000088",
                                          "15:4:#ff112233:#ffaabbcc" } );
}

TEST_CASE( "frame: the search pattern highlight covers its matches", "[render][frame]" )
{
    klogg::render::PatternHighlightOptions pattern;
    pattern.pattern = "failure";
    pattern.caseSensitive = true;
    pattern.useRegex = true;
    pattern.foreColor = RuleFore;
    pattern.backColor = RuleBack;

    auto input = baseInput( { "ERROR: disk failure" } );
    input.patternHighlight = pattern;

    const auto frame = composeFrame( std::move( input ) );

    REQUIRE( chunkList( frame.lines.front() )
             == std::vector<std::string>{ "0:12:#ff112233:#ffaabbcc", "12:7:#ffff0000:#ff008800",
                                          "19:0:#ff112233:#ffaabbcc" } );
}

TEST_CASE( "frame: colour label words highlight their matches", "[render][frame]" )
{
    QuickHighlighterOptions slot;
    slot.words = { "disk" };
    slot.foreColor = RuleFore;
    slot.backColor = RuleBack;

    auto input = baseInput( { "ERROR: disk failure" } );
    input.quickHighlighters = { slot };

    const auto frame = composeFrame( std::move( input ) );

    REQUIRE( chunkList( frame.lines.front() )
             == std::vector<std::string>{ "0:7:#ff112233:#ffaabbcc", "7:4:#ffff0000:#ff008800",
                                          "11:8:#ff112233:#ffaabbcc" } );
}

TEST_CASE( "frame: the QuickFind pattern highlights the visible lines", "[render][frame]" )
{
    klogg::render::QuickFindOptions quickFind;
    quickFind.pattern = "disk";
    quickFind.caseSensitive = true;
    quickFind.regex = false;
    quickFind.backColor = QuickFindBack;

    auto input = baseInput( { "ERROR: disk failure" } );
    input.quickFind = quickFind;

    const auto frame = composeFrame( std::move( input ) );

    REQUIRE( chunkList( frame.lines.front() )
             == std::vector<std::string>{ "0:7:#ff112233:#ffaabbcc", "7:4:#ff000000:#ffffee00",
                                          "11:8:#ff112233:#ffaabbcc" } );
}

TEST_CASE( "frame: the ANSI parser receives the colours of the previous line", "[render][frame]" )
{
    // The paint loop handed the parser the colour locals of its previous
    // iteration, and the parser drops a run whose colour equals the default it
    // was given: the same escape sequence paints differently depending on the
    // line above it.
    const Rgba ansiRed{ 0xcd, 0x00, 0x00, 0xff };
    const auto set = ruleSet( { lineRule( "plain", ansiRed ) } );

    auto input = baseInput( { "plain", "[31mRED", "[31mRED" } );
    input.ansiProcessing = AnsiProcessing::RenderColors;
    input.highlighterSet = &set;

    const auto frame = composeFrame( std::move( input ) );

    // The first line carries the rule colour, so the parser default of the
    // second one is that same colour and its run is dropped: the text falls
    // back to the palette colour of its own line
    REQUIRE( frame.lines[ 0 ].foreColor == ansiRed );
    REQUIRE( chunkList( frame.lines[ 1 ] )
             == std::vector<std::string>{ "0:3:#ff112233:#ffaabbcc" } );

    // The third line follows a palette coloured one, so its run survives
    REQUIRE( chunkList( frame.lines[ 2 ] )
             == std::vector<std::string>{ "0:3:#ffcd0000:#ffaabbcc", "3:0:#ff112233:#ffaabbcc" } );
}

TEST_CASE( "frame: an empty frame stays empty", "[render][frame]" )
{
    const auto frame = composeFrame( baseInput( {} ) );

    REQUIRE( frame.lines.empty() );
}

TEST_CASE( "frame: an empty line keeps one part and no run", "[render][frame]" )
{
    const auto frame = composeFrame( baseInput( { "" } ) );

    REQUIRE( frame.lines.size() == 1 );
    REQUIRE( frame.lines.front().displayText.empty() );
    REQUIRE( partList( frame.lines.front() ) == std::vector<std::string>{ "0:0" } );
}
