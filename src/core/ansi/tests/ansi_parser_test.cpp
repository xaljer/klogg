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

#include <catch2/catch.hpp>

#include <string>
#include <utility>
#include <vector>

#include "ansi_parser.h"

using klogg::Rgba;

static const Rgba kDefaultFg( 255, 255, 255 );
static const Rgba kDefaultBg( 0, 0, 0 );

static const Rgba kRed( 205, 0, 0 );
static const Rgba kGreen( 0, 205, 0 );
static const Rgba kCyan( 0, 205, 205 );
static const Rgba kBrightRed( 255, 0, 0 );
static const Rgba kBrightYellow( 255, 255, 0 );

// Placed in none of the palette tables, so a color table sweep can assert that
// every entry produces a segment (a run equal to the default is dropped).
static const Rgba kProbeDefaultFg( 0x0a, 0x0b, 0x0c );

SCENARIO( "AnsiSgrParser with no ANSI codes", "[ansi]" )
{
    GIVEN( "A plain text line" )
    {
        const std::string line = "plain text without codes";

        WHEN( "mode is None" )
        {
            const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::None, kDefaultFg,
                                                          kDefaultBg );
            THEN( "text passes through unchanged" )
            {
                REQUIRE( result.cleanText == line );
                REQUIRE( result.segments.empty() );
            }
        }

        WHEN( "mode is StripOnly" )
        {
            const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::StripOnly,
                                                          kDefaultFg, kDefaultBg );
            THEN( "text passes through unchanged" )
            {
                REQUIRE( result.cleanText == line );
                REQUIRE( result.segments.empty() );
            }
        }

        WHEN( "mode is RenderColors" )
        {
            const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors,
                                                          kDefaultFg, kDefaultBg );
            THEN( "text passes through unchanged" )
            {
                REQUIRE( result.cleanText == line );
                REQUIRE( result.segments.empty() );
            }
        }
    }
}

SCENARIO( "AnsiSgrParser None mode preserves raw codes", "[ansi]" )
{
    GIVEN( "A line with ANSI codes" )
    {
        const std::string line = "\033[31mRed\033[0m";

        WHEN( "mode is None" )
        {
            const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::None, kDefaultFg,
                                                          kDefaultBg );
            THEN( "raw codes are preserved" )
            {
                REQUIRE( result.cleanText == line );
                REQUIRE( result.segments.empty() );
            }
        }
    }
}

SCENARIO( "AnsiSgrParser basic SGR foreground colors", "[ansi]" )
{
    GIVEN( "A line with a single SGR foreground color" )
    {

        WHEN( "color is red (31)" )
        {
            const std::string line = "\033[31mRed text";
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text is stripped and colored red" )
            {
                REQUIRE( result.cleanText == "Red text" );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].startColumn == 0 );
                REQUIRE( result.segments[ 0 ].length == 8 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
            }
        }

        WHEN( "color is green (32)" )
        {
            const std::string line = "\033[32mGreen";
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text is green" )
            {
                REQUIRE( result.cleanText == "Green" );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == kGreen );
            }
        }

        WHEN( "color is cyan (36)" )
        {
            const std::string line = "\033[36mDEBUG";
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text is cyan" )
            {
                REQUIRE( result.cleanText == "DEBUG" );
                REQUIRE( result.segments[ 0 ].foreColor == kCyan );
            }
        }
    }
}

SCENARIO( "AnsiSgrParser bright foreground colors", "[ansi]" )
{
    GIVEN( "A line with bright SGR color (90-97)" )
    {

        WHEN( "color is bright red (91)" )
        {
            const std::string line = "\033[91mBright";
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text is bright red" )
            {
                REQUIRE( result.cleanText == "Bright" );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == kBrightRed );
            }
        }

        WHEN( "color is bright yellow (93)" )
        {
            const std::string line = "\033[93mWarning";
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text is bright yellow" )
            {
                REQUIRE( result.cleanText == "Warning" );
                REQUIRE( result.segments[ 0 ].foreColor == kBrightYellow );
            }
        }
    }
}

SCENARIO( "AnsiSgrParser SGR reset", "[ansi]" )
{
    GIVEN( "A line with color then reset" )
    {
        const std::string line = "\033[31mRed\033[0m normal";

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "two segments: red then default" )
            {
                REQUIRE( result.cleanText == "Red normal" );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].startColumn == 0 );
                REQUIRE( result.segments[ 0 ].length == 3 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
            }
        }
    }

    GIVEN( "Multiple color changes with reset" )
    {
        const std::string line = "\033[31mRed\033[32mGreen\033[0m";

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "two segments before reset" )
            {
                REQUIRE( result.cleanText == "RedGreen" );
                REQUIRE( result.segments.size() == 2 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
                REQUIRE( result.segments[ 1 ].foreColor == kGreen );
            }
        }
    }
}

SCENARIO( "AnsiSgrParser mid-line color changes", "[ansi]" )
{
    GIVEN( "Text with ANSI in the middle" )
    {
        const std::string line = "Hello \033[31mWorld\033[0m!";

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "three segments: default, red, default" )
            {
                REQUIRE( result.cleanText == "Hello World!" );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].startColumn == 6 );
                REQUIRE( result.segments[ 0 ].length == 5 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
            }
        }
    }

    GIVEN( "Multiple mid-line color switches" )
    {
        const std::string line = "A\033[31mB\033[32mC\033[0mD";

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "correctly tracks column positions" )
            {
                REQUIRE( result.cleanText == "ABCD" );
                REQUIRE( result.segments.size() == 2 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
                REQUIRE( result.segments[ 1 ].foreColor == kGreen );
                REQUIRE( result.segments[ 0 ].length == 1 );
                REQUIRE( result.segments[ 1 ].length == 1 );
            }
        }
    }
}

SCENARIO( "AnsiSgrParser 256-color (38;5;n)", "[ansi]" )
{
    GIVEN( "A line with 256-color foreground" )
    {

        WHEN( "color is index 196 (bright red)" )
        {
            const std::string line = "\033[38;5;196mColor256";
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text uses 256-color value" )
            {
                REQUIRE( result.cleanText == "Color256" );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == Rgba( 255, 0, 0 ) );
            }
        }

        WHEN( "color is index 51 (cyan from cube)" )
        {
            const std::string line = "\033[38;5;51mCyan";
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text uses cube color" )
            {
                REQUIRE( result.cleanText == "Cyan" );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == Rgba( 0, 255, 255 ) );
            }
        }

        WHEN( "color is index 240 (grayscale)" )
        {
            const std::string line = "\033[38;5;240mGray";
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text uses grayscale" )
            {
                REQUIRE( result.cleanText == "Gray" );
                REQUIRE( result.segments.size() == 1 );
                const int expectedGray = ( 240 - 232 ) * 10 + 8;
                REQUIRE( result.segments[ 0 ].foreColor
                         == Rgba( static_cast<uint8_t>( expectedGray ),
                                  static_cast<uint8_t>( expectedGray ),
                                  static_cast<uint8_t>( expectedGray ) ) );
            }
        }
    }
}

SCENARIO( "AnsiSgrParser true color (38;2;r;g;b)", "[ansi]" )
{
    GIVEN( "A line with true color foreground" )
    {
        const std::string line = "\033[38;2;255;128;0mTrueColor";

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text uses specified RGB" )
            {
                REQUIRE( result.cleanText == "TrueColor" );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == Rgba( 255, 128, 0 ) );
            }
        }
    }

    GIVEN( "True color mixed with reset" )
    {
        const std::string line = "\033[38;2;100;200;50mCustom\033[0m default";

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "custom color segment followed by default" )
            {
                REQUIRE( result.cleanText == "Custom default" );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == Rgba( 100, 200, 50 ) );
            }
        }
    }
}

SCENARIO( "AnsiSgrParser StripOnly mode", "[ansi]" )
{
    GIVEN( "A line with ANSI color codes" )
    {
        const std::string line = "\033[31mRed\033[0m";

        WHEN( "mode is StripOnly" )
        {
            const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::StripOnly,
                                                          kDefaultFg, kDefaultBg );

            THEN( "codes removed, no segments produced" )
            {
                REQUIRE( result.cleanText == "Red" );
                REQUIRE( result.segments.empty() );
            }
        }
    }

    GIVEN( "Complex line in StripOnly mode" )
    {
        const std::string line = "Hello \033[38;5;196mWorld\033[0m!";

        WHEN( "parsed in StripOnly mode" )
        {
            const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::StripOnly,
                                                          kDefaultFg, kDefaultBg );

            THEN( "all codes removed, no segments" )
            {
                REQUIRE( result.cleanText == "Hello World!" );
                REQUIRE( result.segments.empty() );
            }
        }
    }
}

SCENARIO( "AnsiSgrParser non-SGR CSI sequences", "[ansi]" )
{
    GIVEN( "A line with cursor or erase CSI sequences" )
    {
        WHEN( "CSI K (erase in line)" )
        {
            const std::string line = "\033[2KHello";
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "CSI is stripped, no color segment" )
            {
                REQUIRE( result.cleanText == "Hello" );
                REQUIRE( result.segments.empty() );
            }
        }

        WHEN( "CSI H (cursor position)" )
        {
            const std::string line = "\033[1;1HStart";
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "CSI is stripped" )
            {
                REQUIRE( result.cleanText == "Start" );
                REQUIRE( result.segments.empty() );
            }
        }

        WHEN( "Mixed SGR and non-SGR CSI" )
        {
            const std::string line = "\033[2K\033[31mRed";
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "non-SGR stripped, SGR applied" )
            {
                REQUIRE( result.cleanText == "Red" );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
            }
        }
    }
}

SCENARIO( "AnsiSgrParser edge cases", "[ansi]" )
{
    GIVEN( "An empty line" )
    {
        const std::string line;

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "returns empty clean text" )
            {
                REQUIRE( result.cleanText.empty() );
                REQUIRE( result.segments.empty() );
            }
        }
    }

    GIVEN( "A line with only ANSI codes" )
    {
        const std::string line = "\033[31m\033[32m\033[0m";

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "clean text is empty" )
            {
                REQUIRE( result.cleanText.empty() );
                REQUIRE( result.segments.empty() );
            }
        }
    }

    GIVEN( "ANSI code at start with color spanning to end" )
    {
        const std::string line = "\033[31mRed no reset";

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "entire text is red" )
            {
                REQUIRE( result.cleanText == "Red no reset" );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
                REQUIRE( result.segments[ 0 ].startColumn == 0 );
                REQUIRE( result.segments[ 0 ].length == 12 );
            }
        }
    }

    GIVEN( "Default foreground reset (39)" )
    {
        const std::string line = "\033[31mRed\033[39m default";

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "39 resets to default foreground" )
            {
                REQUIRE( result.cleanText == "Red default" );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
            }
        }
    }

    GIVEN( "Bold foreground (1;31)" )
    {
        const std::string line = "\033[1;31mBoldRed";

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "bold is ignored, color applied" )
            {
                REQUIRE( result.cleanText == "BoldRed" );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
            }
        }
    }
}

SCENARIO( "AnsiSgrParser standard colors (30-37 and 90-97)", "[ansi]" )
{
    const auto parseColored = []( int code ) {
        return AnsiSgrParser::parseLine( "\033[" + std::to_string( code ) + "mtext",
                                         AnsiProcessing::RenderColors, kProbeDefaultFg,
                                         kDefaultBg );
    };

    GIVEN( "The eight standard foreground colors" )
    {
        const std::vector<std::pair<int, Rgba>> standard = {
            { 30, Rgba( 0, 0, 0 ) },     { 31, Rgba( 205, 0, 0 ) },     { 32, Rgba( 0, 205, 0 ) },
            { 33, Rgba( 205, 205, 0 ) }, { 34, Rgba( 0, 0, 238 ) },     { 35, Rgba( 205, 0, 205 ) },
            { 36, Rgba( 0, 205, 205 ) }, { 37, Rgba( 229, 229, 229 ) },
        };

        for ( const auto& entry : standard ) {
            INFO( "SGR " << entry.first );
            const auto result = parseColored( entry.first );
            REQUIRE( result.cleanText == "text" );
            REQUIRE( result.segments.size() == 1 );
            REQUIRE( result.segments[ 0 ].foreColor == entry.second );
        }
    }

    GIVEN( "The eight bright foreground colors (90-97)" )
    {
        const std::vector<std::pair<int, Rgba>> bright = {
            { 90, Rgba( 127, 127, 127 ) }, { 91, Rgba( 255, 0, 0 ) },
            { 92, Rgba( 0, 255, 0 ) },     { 93, Rgba( 255, 255, 0 ) },
            { 94, Rgba( 92, 92, 255 ) },   { 95, Rgba( 255, 0, 255 ) },
            { 96, Rgba( 0, 255, 255 ) },   { 97, Rgba( 255, 255, 255 ) },
        };

        for ( const auto& entry : bright ) {
            INFO( "SGR " << entry.first );
            const auto result = parseColored( entry.first );
            REQUIRE( result.cleanText == "text" );
            REQUIRE( result.segments.size() == 1 );
            REQUIRE( result.segments[ 0 ].foreColor == entry.second );
        }
    }
}

SCENARIO( "AnsiSgrParser 38;5 below 16 reuses the standard colors", "[ansi]" )
{
    GIVEN( "Every low palette index" )
    {
        for ( int index = 0; index < 16; ++index ) {
            INFO( "palette index " << index );
            const auto viaIndex = AnsiSgrParser::parseLine(
                "\033[38;5;" + std::to_string( index ) + "mtext", AnsiProcessing::RenderColors,
                kProbeDefaultFg, kDefaultBg );
            const auto colorCode = index < 8 ? 30 + index : 90 + ( index - 8 );
            const auto viaCode = AnsiSgrParser::parseLine(
                "\033[" + std::to_string( colorCode ) + "mtext", AnsiProcessing::RenderColors,
                kProbeDefaultFg, kDefaultBg );

            REQUIRE( viaIndex.segments.size() == 1 );
            REQUIRE( viaCode.segments.size() == 1 );
            REQUIRE( viaIndex.segments[ 0 ].foreColor == viaCode.segments[ 0 ].foreColor );
        }
    }
}

SCENARIO( "AnsiSgrParser 38;5 covers the cube and the gray ramp", "[ansi]" )
{
    const auto parseIndexed = []( int index ) {
        return AnsiSgrParser::parseLine( "\033[38;5;" + std::to_string( index ) + "mtext",
                                         AnsiProcessing::RenderColors, kProbeDefaultFg,
                                         kDefaultBg );
    };

    GIVEN( "Every color cube index (16-231)" )
    {
        for ( int index = 16; index < 232; ++index ) {
            INFO( "cube index " << index );
            const auto n = index - 16;
            const auto expected = Rgba( static_cast<uint8_t>( ( n / 36 ) * 51 ),
                                        static_cast<uint8_t>( ( ( n / 6 ) % 6 ) * 51 ),
                                        static_cast<uint8_t>( ( n % 6 ) * 51 ) );
            const auto result = parseIndexed( index );
            REQUIRE( result.segments.size() == 1 );
            REQUIRE( result.segments[ 0 ].foreColor == expected );
        }
    }

    GIVEN( "Every gray ramp index (232-255)" )
    {
        for ( int index = 232; index < 256; ++index ) {
            INFO( "gray index " << index );
            const auto level = static_cast<uint8_t>( ( index - 232 ) * 10 + 8 );
            const auto expected = Rgba( level, level, level );
            const auto result = parseIndexed( index );
            REQUIRE( result.segments.size() == 1 );
            REQUIRE( result.segments[ 0 ].foreColor == expected );
        }
    }

    GIVEN( "An index past the end of the palette" )
    {
        for ( const int index : { 256, 999 } ) {
            INFO( "out of range index " << index );
            const auto result = parseIndexed( index );
            REQUIRE( result.cleanText == "text" );
            REQUIRE( result.segments.size() == 1 );
            // An unknown index resolves to the invalid (transparent) sentinel
            REQUIRE( result.segments[ 0 ].foreColor.a == 0 );
        }
    }
}

SCENARIO( "AnsiSgrParser swallows CSI sequences cut by the end of the line", "[ansi]" )
{
    GIVEN( "An unterminated sequence" )
    {
        for ( const auto& line :
              { std::string( "\033[31" ), std::string( "\033[" ), std::string( "\033" ) } ) {
            INFO( "sequence of " << line.size() << " bytes" );
            const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors,
                                                          kDefaultFg, kDefaultBg );
            REQUIRE( result.cleanText.empty() );
            REQUIRE( result.segments.empty() );
        }
    }

    GIVEN( "An SGR sequence with too few parameters" )
    {
        for ( const auto& line :
              { std::string( "\033[38;5mtext" ), std::string( "\033[38;2;1;2mtext" ) } ) {
            INFO( "line " << line );
            const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors,
                                                          kDefaultFg, kDefaultBg );
            // The command is ignored, the text survives and stays uncolored
            REQUIRE( result.cleanText == "text" );
            REQUIRE( result.segments.empty() );
        }
    }
}

SCENARIO( "AnsiSgrParser treats a bare SGR as a reset", "[ansi]" )
{
    GIVEN( "An empty parameter list" )
    {
        const std::string line = "\033[31mred\033[mmore";
        const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors,
                                                      kDefaultFg, kDefaultBg );

        THEN( "the colored run ends at the bare SGR" )
        {
            REQUIRE( result.cleanText == "redmore" );
            REQUIRE( result.segments.size() == 1 );
            REQUIRE( result.segments[ 0 ].startColumn == 0 );
            REQUIRE( result.segments[ 0 ].length == 3 );
            REQUIRE( result.segments[ 0 ].foreColor == kRed );
        }
    }
}

SCENARIO( "AnsiSgrParser only understands CSI sequences, not OSC", "[ansi]" )
{
    // Known limitation, pinned here so a deliberate change is visible: CSI
    // terminators are letters, so an OSC payload is consumed up to its first
    // letter and the rest of it is emitted as text.
    GIVEN( "An OSC title sequence" )
    {
        const std::string line = "\033]0;title\007visible";
        const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors,
                                                      kDefaultFg, kDefaultBg );

        THEN( "the payload tail is kept as text" )
        {
            REQUIRE( result.cleanText == "itle\007visible" );
            REQUIRE( result.segments.empty() );
        }
    }
}

SCENARIO( "AnsiSgrParser measures columns in UTF-16 code units", "[ansi]" )
{
    // Columns are UTF-16 code units - the space LineColumn and QString::mid()
    // use - not the byte offsets of the UTF-8 text.
    GIVEN( "A colored run of multi-byte characters" )
    {
        // "中文" is six bytes but two code units, followed by uncolored " tail"
        const std::string line = "\033[31m\xE4\xB8\xAD\xE6\x96\x87\033[0m tail";
        const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors,
                                                      kDefaultFg, kDefaultBg );

        THEN( "stripping preserves the encoded text" )
        {
            REQUIRE( result.cleanText == "\xE4\xB8\xAD\xE6\x96\x87 tail" );
            REQUIRE( result.segments.size() == 1 );
            REQUIRE( result.segments[ 0 ].foreColor == kRed );
        }

        THEN( "the segment counts code units, not bytes" )
        {
            REQUIRE( result.segments[ 0 ].startColumn == 0 );
            REQUIRE( result.segments[ 0 ].length == 2 );
        }
    }

    GIVEN( "A colour that starts after multi-byte text" )
    {
        const std::string line = "\xE4\xB8\xAD\xE6\x96\x87\033[31mred";
        const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors,
                                                      kDefaultFg, kDefaultBg );

        THEN( "the start column is the code unit count of the text before it" )
        {
            REQUIRE( result.cleanText == "\xE4\xB8\xAD\xE6\x96\x87red" );
            REQUIRE( result.segments.size() == 1 );
            REQUIRE( result.segments[ 0 ].startColumn == 2 );
            REQUIRE( result.segments[ 0 ].length == 3 );
        }
    }

    GIVEN( "A character outside the basic plane" )
    {
        // U+1F600 is four bytes in UTF-8 and a surrogate pair in UTF-16
        const std::string line = "\033[31m\xF0\x9F\x98\x80\033[0m";
        const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors,
                                                      kDefaultFg, kDefaultBg );

        THEN( "it counts as two code units" )
        {
            REQUIRE( result.cleanText == "\xF0\x9F\x98\x80" );
            REQUIRE( result.segments.size() == 1 );
            REQUIRE( result.segments[ 0 ].startColumn == 0 );
            REQUIRE( result.segments[ 0 ].length == 2 );
        }
    }
}

SCENARIO( "AnsiSgrParser drops a run that matches the default foreground", "[ansi]" )
{
    GIVEN( "An SGR colour equal to the line default" )
    {
        const std::string line = "\033[97mtext";
        const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors,
                                                      kDefaultFg, kDefaultBg );

        THEN( "no segment is reported for it" )
        {
            REQUIRE( result.cleanText == "text" );
            REQUIRE( result.segments.empty() );
        }
    }
}

SCENARIO( "processDisplayLine passes lines through when no work is needed", "[ansi]" )
{
    GIVEN( "Colour rendering is disabled" )
    {
        const auto result = AnsiSgrParser::processDisplayLine(
            std::string( "\033[31mred\033[0m" ), AnsiProcessing::None, kDefaultFg, kDefaultBg );

        THEN( "the raw line is returned untouched" )
        {
            REQUIRE( result.text == "\033[31mred\033[0m" );
            REQUIRE( result.segments.empty() );
        }
    }

    GIVEN( "A line without escape sequences" )
    {
        for ( const auto mode : { AnsiProcessing::StripOnly, AnsiProcessing::RenderColors } ) {
            INFO( "mode " << static_cast<int>( mode ) );
            const auto result = AnsiSgrParser::processDisplayLine( std::string( "plain text" ),
                                                                   mode, kDefaultFg, kDefaultBg );
            REQUIRE( result.text == "plain text" );
            REQUIRE( result.segments.empty() );
        }
    }

    GIVEN( "An empty line" )
    {
        for ( const auto mode :
              { AnsiProcessing::None, AnsiProcessing::StripOnly, AnsiProcessing::RenderColors } ) {
            INFO( "mode " << static_cast<int>( mode ) );
            const auto result
                = AnsiSgrParser::processDisplayLine( std::string{}, mode, kDefaultFg, kDefaultBg );
            REQUIRE( result.text.empty() );
            REQUIRE( result.segments.empty() );
        }
    }
}

SCENARIO( "processDisplayLine agrees with parseLine", "[ansi]" )
{
    GIVEN( "A line with two coloured runs" )
    {
        const std::string line = "a\033[31mred\033[0mb\033[32mgreen\033[0m";

        for ( const auto mode : { AnsiProcessing::StripOnly, AnsiProcessing::RenderColors } ) {
            INFO( "mode " << static_cast<int>( mode ) );
            const auto parsed = AnsiSgrParser::parseLine( line, mode, kDefaultFg, kDefaultBg );
            const auto displayed
                = AnsiSgrParser::processDisplayLine( line, mode, kDefaultFg, kDefaultBg );

            REQUIRE( displayed.text == parsed.cleanText );
            REQUIRE( displayed.segments.size() == parsed.segments.size() );
            for ( std::size_t index = 0; index < parsed.segments.size(); ++index ) {
                REQUIRE( displayed.segments[ index ].startColumn
                         == parsed.segments[ index ].startColumn );
                REQUIRE( displayed.segments[ index ].length == parsed.segments[ index ].length );
                REQUIRE( displayed.segments[ index ].foreColor
                         == parsed.segments[ index ].foreColor );
            }
        }
    }
}
