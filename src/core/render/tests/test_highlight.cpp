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

#include <cstdint>
#include <string>
#include <vector>

#include "highlight.h"

// Golden tests for the Qt-free highlight composer. The expectations below
// transcribe the behaviour of the paint path's highlight composition
// (AbstractLogView::computeLineHighlightMatches plus the selection portion the
// drawTextArea loop appended after it): contributors are applied in order, each
// one overwriting the ranges already collected.
namespace {

using klogg::render::composeLineHighlights;
using klogg::render::LineHighlightInput;
using klogg::render::LineHighlightResult;

const klogg::Rgba Red{ 0xff, 0x00, 0x00 };
const klogg::Rgba Green{ 0x00, 0xff, 0x00 };
const klogg::Rgba Blue{ 0x00, 0x00, 0xff };
const klogg::Rgba Yellow{ 0xff, 0xff, 0x00 };
const klogg::Rgba Cyan{ 0x00, 0xff, 0xff };
const klogg::Rgba Magenta{ 0xff, 0x00, 0xff };

// "start:size:fore:back" per match, with an empty color string for alpha 0.
std::vector<std::string> matchList( const HighlightedMatchRanges& ranges )
{
    std::vector<std::string> result;
    for ( const auto& match : ranges.matches() ) {
        result.push_back( std::to_string( match.startColumn().get() ) + ":"
                          + std::to_string( match.size().get() ) + ":" + match.foreColor().toHex()
                          + ":" + match.backColor().toHex() );
    }
    return result;
}

Highlighter wordRule( const std::string& pattern, const klogg::Rgba& fore,
                      const klogg::Rgba& back = klogg::Rgba{} )
{
    return Highlighter{ pattern, false, true, fore, back };
}

Highlighter lineRule( const std::string& pattern, const klogg::Rgba& fore,
                      const klogg::Rgba& back = klogg::Rgba{} )
{
    return Highlighter{ pattern, false, false, fore, back };
}

HighlighterSet ruleSet( const klogg::vector<Highlighter>& rules )
{
    auto set = HighlighterSet::createNewSet( "render test" );
    set.setHighlighters( rules );
    return set;
}

klogg::vector<AnsiSegment> ansiSegments( std::vector<std::pair<int, int>> spans,
                                         const klogg::Rgba& fore )
{
    klogg::vector<AnsiSegment> segments;
    for ( const auto& span : spans ) {
        segments.push_back( AnsiSegment{ span.first, span.second, fore } );
    }
    return segments;
}

LineHighlightInput baseInput( std::string_view line )
{
    LineHighlightInput input;
    input.line = line;
    input.lineNumber = 0_lnum;
    input.searchStartIndex = 0_lnum;
    input.searchEndIndex = 100_lnum;
    return input;
}

} // namespace

TEST_CASE( "compose: word rule colours its match only", "[render][highlight]" )
{
    const auto set = ruleSet( { wordRule( "ERROR", Red ) } );

    auto input = baseInput( "ERROR: disk failure" );
    input.highlighterSet = &set;

    const auto result = composeLineHighlights( input );

    REQUIRE( result.matchType == HighlighterMatchType::WordMatch );
    REQUIRE( matchList( result.matches ) == std::vector<std::string>{ "0:5:#ffff0000:" } );
    // A word rule says nothing about the line itself
    REQUIRE( result.lineForeColor.a == 0 );
    REQUIRE( result.lineBackColor.a == 0 );
}

TEST_CASE( "compose: whole line rule reports line colours and covers the line",
           "[render][highlight]" )
{
    const auto set = ruleSet( { lineRule( "ERROR", Red, Blue ) } );

    auto input = baseInput( "ERROR: disk failure" );
    input.highlighterSet = &set;

    const auto result = composeLineHighlights( input );

    REQUIRE( result.matchType == HighlighterMatchType::LineMatch );
    REQUIRE( matchList( result.matches )
             == std::vector<std::string>{ "0:19:#ffff0000:#ff0000ff" } );
    REQUIRE( result.lineForeColor == Red );
    REQUIRE( result.lineBackColor == Blue );
}

TEST_CASE( "compose: whole line rule without a back colour leaves it unset", "[render][highlight]" )
{
    SECTION( "no back colour (theme background)" )
    {
        const auto set = ruleSet( { lineRule( "ERROR", Red ) } );

        auto input = baseInput( "ERROR: disk failure" );
        input.highlighterSet = &set;

        const auto result = composeLineHighlights( input );

        REQUIRE( result.lineForeColor == Red );
        // Alpha 0 is the caller's signal to keep its own line background
        REQUIRE( result.lineBackColor.a == 0 );
        REQUIRE( result.matches.front().backColor().a == 0 );
    }

    SECTION( "explicit back colour" )
    {
        const auto set = ruleSet( { lineRule( "ERROR", Red, Blue ) } );

        auto input = baseInput( "ERROR: disk failure" );
        input.highlighterSet = &set;

        const auto result = composeLineHighlights( input );

        REQUIRE( result.lineBackColor == Blue );
    }
}

TEST_CASE( "compose: a whole line rule clears the matches collected before it",
           "[render][highlight]" )
{
    // Rules run from the end of the list to the front, so the word rule of the
    // second entry adds its match first and the whole line rule of the first
    // entry wipes it.
    const auto set = ruleSet( { lineRule( "ERROR", Blue ), wordRule( "ERROR", Red ) } );

    auto input = baseInput( "ERROR: disk failure" );
    input.highlighterSet = &set;

    const auto result = composeLineHighlights( input );

    REQUIRE( result.matchType == HighlighterMatchType::LineMatch );
    REQUIRE( matchList( result.matches ) == std::vector<std::string>{ "0:19:#ff0000ff:" } );
    // Taken from the ranges as the set left them
    REQUIRE( result.lineForeColor == Blue );
    REQUIRE( result.lineBackColor.a == 0 );
}

TEST_CASE( "compose: word rules are layered over a whole line rule", "[render][highlight]" )
{
    const auto set = ruleSet( { wordRule( "ERROR", Red ), lineRule( "ERROR", Blue ) } );

    auto input = baseInput( "ERROR: disk failure" );
    input.highlighterSet = &set;

    const auto result = composeLineHighlights( input );

    REQUIRE( result.matchType == HighlighterMatchType::LineMatch );
    REQUIRE( matchList( result.matches )
             == std::vector<std::string>{ "0:5:#ffff0000:", "5:14:#ff0000ff:" } );
    // The line colours are read from the ranges as the set left them, so the
    // word rule that landed in front supplies the foreground here.
    REQUIRE( result.lineForeColor == Red );
    REQUIRE( result.lineBackColor.a == 0 );
}

TEST_CASE( "compose: ANSI segments overwrite the rules of the set", "[render][highlight]" )
{
    const auto set = ruleSet( { wordRule( "ERROR", Red ) } );
    const auto segments = ansiSegments( { { 0, 7 } }, Green );

    auto input = baseInput( "ERROR: disk failure" );
    input.highlighterSet = &set;
    input.ansiSegments = &segments;
    input.ansiProcessing = AnsiProcessing::RenderColors;

    const auto result = composeLineHighlights( input );

    REQUIRE( matchList( result.matches ) == std::vector<std::string>{ "0:7:#ff00ff00:" } );
}

TEST_CASE( "compose: ANSI segments keep their own order", "[render][highlight]" )
{
    const auto first = ansiSegments( { { 0, 5 } }, Red );
    const auto second = ansiSegments( { { 2, 6 } }, Green );

    klogg::vector<AnsiSegment> segments = first;
    segments.insert( segments.end(), second.begin(), second.end() );

    auto input = baseInput( "ERROR: disk failure" );
    input.ansiSegments = &segments;
    input.ansiProcessing = AnsiProcessing::RenderColors;

    const auto result = composeLineHighlights( input );

    REQUIRE( matchList( result.matches )
             == std::vector<std::string>{ "0:2:#ffff0000:", "2:6:#ff00ff00:" } );
}

TEST_CASE( "compose: ANSI segments need the rendering mode", "[render][highlight]" )
{
    const auto segments = ansiSegments( { { 0, 7 } }, Green );

    auto input = baseInput( "ERROR: disk failure" );
    input.ansiSegments = &segments;
    input.ansiProcessing = AnsiProcessing::StripOnly;

    const auto result = composeLineHighlights( input );

    REQUIRE( result.matches.empty() );
}

TEST_CASE( "compose: ANSI colours survive outside the search range", "[render][highlight]" )
{
    const auto set = ruleSet( { wordRule( "ERROR", Red ) } );
    const auto segments = ansiSegments( { { 0, 7 } }, Green );
    const auto external = klogg::vector<HighlightedMatch>{ HighlightedMatch{ 0_lcol, 5_length, Cyan,
                                                                             klogg::Rgba{} } };

    auto input = baseInput( "ERROR: disk failure" );
    input.lineNumber = 5_lnum;
    input.searchStartIndex = 0_lnum;
    input.searchEndIndex = 3_lnum;
    input.highlighterSet = &set;
    input.ansiSegments = &segments;
    input.ansiProcessing = AnsiProcessing::RenderColors;
    input.externalMatches = &external;

    const auto result = composeLineHighlights( input );

    REQUIRE( result.matchType == HighlighterMatchType::NoMatch );
    REQUIRE( result.lineForeColor.a == 0 );
    REQUIRE( matchList( result.matches ) == std::vector<std::string>{ "0:7:#ff00ff00:" } );
}

TEST_CASE( "compose: the search pattern overwrites ANSI segments", "[render][highlight]" )
{
    const auto segments = ansiSegments( { { 0, 7 } }, Green );
    const auto pattern = wordRule( "ERROR", Blue );

    auto input = baseInput( "ERROR: disk failure" );
    input.ansiSegments = &segments;
    input.ansiProcessing = AnsiProcessing::RenderColors;
    input.patternHighlight = &pattern;

    const auto result = composeLineHighlights( input );

    REQUIRE( matchList( result.matches )
             == std::vector<std::string>{ "0:5:#ff0000ff:", "5:2:#ff00ff00:" } );
}

TEST_CASE( "compose: quick highlighters overwrite the search pattern", "[render][highlight]" )
{
    const auto pattern = wordRule( "ERROR", Blue );
    const auto quickHighlighters = klogg::vector<Highlighter>{ wordRule( "ERROR", Yellow ) };

    auto input = baseInput( "ERROR: disk failure" );
    input.patternHighlight = &pattern;
    input.quickHighlighters = &quickHighlighters;

    const auto result = composeLineHighlights( input );

    REQUIRE( matchList( result.matches ) == std::vector<std::string>{ "0:5:#ffffff00:" } );
}

TEST_CASE( "compose: external matches overwrite the quick highlighters", "[render][highlight]" )
{
    const auto quickHighlighters = klogg::vector<Highlighter>{ wordRule( "ERROR", Yellow ) };
    const auto external = klogg::vector<HighlightedMatch>{ HighlightedMatch{ 2_lcol, 3_length, Cyan,
                                                                             klogg::Rgba{} } };

    auto input = baseInput( "ERROR: disk failure" );
    input.quickHighlighters = &quickHighlighters;
    input.externalMatches = &external;

    const auto result = composeLineHighlights( input );

    REQUIRE( matchList( result.matches )
             == std::vector<std::string>{ "0:2:#ffffff00:", "2:3:#ff00ffff:" } );
}

TEST_CASE( "compose: the selection portion is applied last", "[render][highlight]" )
{
    const auto external = klogg::vector<HighlightedMatch>{ HighlightedMatch{ 0_lcol, 5_length, Cyan,
                                                                             klogg::Rgba{} } };
    const HighlightedMatch selection{ 3_lcol, 4_length, Magenta, klogg::Rgba{} };

    auto input = baseInput( "ERROR: disk failure" );
    input.externalMatches = &external;
    input.selectionPortion = &selection;

    const auto result = composeLineHighlights( input );

    REQUIRE( matchList( result.matches )
             == std::vector<std::string>{ "0:3:#ff00ffff:", "3:4:#ffff00ff:" } );
}

TEST_CASE( "compose: the selection portion applies outside the search range",
           "[render][highlight]" )
{
    const auto set = ruleSet( { wordRule( "ERROR", Red ) } );
    const HighlightedMatch selection{ 3_lcol, 4_length, Magenta, klogg::Rgba{} };

    auto input = baseInput( "ERROR: disk failure" );
    input.lineNumber = 5_lnum;
    input.searchStartIndex = 0_lnum;
    input.searchEndIndex = 3_lnum;
    input.highlighterSet = &set;
    input.selectionPortion = &selection;

    const auto result = composeLineHighlights( input );

    REQUIRE( matchList( result.matches ) == std::vector<std::string>{ "3:4:#ffff00ff:" } );
}

TEST_CASE( "compose: whole line selection is the caller's business", "[render][highlight]" )
{
    // The paint path skips the composition entirely for a line selected as a
    // whole and paints it in the palette colours; an untouched input produces
    // no highlights at all.
    const auto result = composeLineHighlights( baseInput( "ERROR: disk failure" ) );

    REQUIRE( result.matchType == HighlighterMatchType::NoMatch );
    REQUIRE( result.matches.empty() );
    REQUIRE( result.lineForeColor.a == 0 );
    REQUIRE( result.lineBackColor.a == 0 );
}

TEST_CASE( "compose: match columns count UTF-16 code units", "[render][highlight]" )
{
    SECTION( "CJK" )
    {
        const auto set = ruleSet( { wordRule( "ERROR", Red ) } );

        // U+8B66 U+8A00, one UTF-16 unit each; the source stays ASCII so MSVC
        // reads the same bytes whatever the system code page is
        auto input = baseInput( "\xe8\xad\xa6\xe5\x91\x8a ERROR" );
        input.highlighterSet = &set;

        const auto result = composeLineHighlights( input );

        // Two CJK code points and a space are one UTF-16 unit each
        REQUIRE( matchList( result.matches ) == std::vector<std::string>{ "3:5:#ffff0000:" } );
    }

    SECTION( "non-BMP code point" )
    {
        const auto set = ruleSet( { lineRule( "ERROR", Red ) } );

        auto input = baseInput( "\xf0\x9f\x99\x82 ERROR" );
        input.highlighterSet = &set;

        const auto result = composeLineHighlights( input );

        // The emoji is a surrogate pair, so it takes two units
        REQUIRE( matchList( result.matches ) == std::vector<std::string>{ "0:8:#ffff0000:" } );
        REQUIRE( result.matchType == HighlighterMatchType::LineMatch );
    }
}

TEST_CASE( "compose: a whole line rule matches an empty line without columns",
           "[render][highlight]" )
{
    const auto set = ruleSet( { lineRule( "^$", Red, Blue ) } );

    auto input = baseInput( "" );
    input.highlighterSet = &set;

    const auto result = composeLineHighlights( input );

    REQUIRE( result.matchType == HighlighterMatchType::LineMatch );
    // A zero width whole line match still carries the line colours
    REQUIRE( matchList( result.matches ) == std::vector<std::string>{ "0:0:#ffff0000:#ff0000ff" } );
    REQUIRE( result.lineForeColor == Red );
    REQUIRE( result.lineBackColor == Blue );
}

TEST_CASE( "compose: variated colours are deterministic per matched text", "[render][highlight]" )
{
    auto variated = wordRule( "ERROR|WARN", Red );
    variated.setVariateColors( true );
    variated.setColorVariance( 15 );

    const auto set = ruleSet( { variated } );

    auto input = baseInput( "ERROR and WARN" );
    input.highlighterSet = &set;

    const auto first = composeLineHighlights( input );
    const auto second = composeLineHighlights( input );
    const auto freshSet = ruleSet( { variated } );
    auto freshInput = baseInput( "ERROR and WARN" );
    freshInput.highlighterSet = &freshSet;
    const auto fresh = composeLineHighlights( freshInput );

    REQUIRE( matchList( first.matches ) == matchList( second.matches ) );
    REQUIRE( matchList( first.matches ) == matchList( fresh.matches ) );
    REQUIRE( first.matches.matches().size() == 2 );

    // Both words carry the variance of the rule, seeded by their own text, and
    // the same word on a different line keeps its colour.
    const auto colors = first.matches.matches();
    for ( const auto& match : colors ) {
        REQUIRE( match.foreColor().a == Red.a );
        REQUIRE( match.foreColor().r >= ( Red.r * 100 ) / 115 );
        REQUIRE( match.foreColor().r <= ( Red.r * 100 ) / 85 );
    }
    REQUIRE( colors.front().foreColor() != colors.back().foreColor() );

    auto otherInput = baseInput( "WARN and ERROR" );
    otherInput.highlighterSet = &set;
    const auto reversed = composeLineHighlights( otherInput );
    REQUIRE( reversed.matches.matches().size() == 2 );
    REQUIRE( reversed.matches.matches().front().foreColor() == colors.back().foreColor() );
}

TEST_CASE( "compose: a disabled rule contributes nothing", "[render][highlight]" )
{
    auto disabled = wordRule( "ERROR", Red );
    disabled.setEnabled( false );

    const auto set = ruleSet( { disabled } );

    auto input = baseInput( "ERROR: disk failure" );
    input.highlighterSet = &set;

    const auto result = composeLineHighlights( input );

    REQUIRE( result.matchType == HighlighterMatchType::NoMatch );
    REQUIRE( result.matches.empty() );
}
