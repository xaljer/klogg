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

#include "ansi_parser.h"

static const QColor kDefaultFg( 255, 255, 255 );
static const QColor kDefaultBg( 0, 0, 0 );

static const QColor kRed( 205, 0, 0 );
static const QColor kGreen( 0, 205, 0 );
static const QColor kCyan( 0, 205, 205 );
static const QColor kBrightRed( 255, 0, 0 );
static const QColor kBrightYellow( 255, 255, 0 );

SCENARIO( "AnsiSgrParser with no ANSI codes", "[ansi]" )
{
    GIVEN( "A plain text line" )
    {
        const QString line = QStringLiteral( "plain text without codes" );

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
        const QString line = QStringLiteral( "\033[31mRed\033[0m" );

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
            const auto line = QStringLiteral( "\033[31mRed text" );
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text is stripped and colored red" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Red text" ) );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].startColumn == 0 );
                REQUIRE( result.segments[ 0 ].length == 8 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
            }
        }

        WHEN( "color is green (32)" )
        {
            const auto line = QStringLiteral( "\033[32mGreen" );
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text is green" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Green" ) );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == kGreen );
            }
        }

        WHEN( "color is cyan (36)" )
        {
            const auto line = QStringLiteral( "\033[36mDEBUG" );
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text is cyan" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "DEBUG" ) );
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
            const auto line = QStringLiteral( "\033[91mBright" );
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text is bright red" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Bright" ) );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == kBrightRed );
            }
        }

        WHEN( "color is bright yellow (93)" )
        {
            const auto line = QStringLiteral( "\033[93mWarning" );
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text is bright yellow" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Warning" ) );
                REQUIRE( result.segments[ 0 ].foreColor == kBrightYellow );
            }
        }
    }
}

SCENARIO( "AnsiSgrParser SGR reset", "[ansi]" )
{
    GIVEN( "A line with color then reset" )
    {
        const auto line = QStringLiteral( "\033[31mRed\033[0m normal" );

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "two segments: red then default" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Red normal" ) );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].startColumn == 0 );
                REQUIRE( result.segments[ 0 ].length == 3 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
            }
        }
    }

    GIVEN( "Multiple color changes with reset" )
    {
        const auto line = QStringLiteral( "\033[31mRed\033[32mGreen\033[0m" );

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "two segments before reset" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "RedGreen" ) );
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
        const auto line = QStringLiteral( "Hello \033[31mWorld\033[0m!" );

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "three segments: default, red, default" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Hello World!" ) );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].startColumn == 6 );
                REQUIRE( result.segments[ 0 ].length == 5 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
            }
        }
    }

    GIVEN( "Multiple mid-line color switches" )
    {
        const auto line = QStringLiteral( "A\033[31mB\033[32mC\033[0mD" );

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "correctly tracks column positions" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "ABCD" ) );
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
            const auto line = QStringLiteral( "\033[38;5;196mColor256" );
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text uses 256-color value" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Color256" ) );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == QColor( 255, 0, 0 ) );
            }
        }

        WHEN( "color is index 51 (cyan from cube)" )
        {
            const auto line = QStringLiteral( "\033[38;5;51mCyan" );
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text uses cube color" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Cyan" ) );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == QColor( 0, 255, 255 ) );
            }
        }

        WHEN( "color is index 240 (grayscale)" )
        {
            const auto line = QStringLiteral( "\033[38;5;240mGray" );
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text uses grayscale" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Gray" ) );
                REQUIRE( result.segments.size() == 1 );
                const int expectedGray = ( 240 - 232 ) * 10 + 8;
                REQUIRE( result.segments[ 0 ].foreColor == QColor( expectedGray, expectedGray,
                                                                    expectedGray ) );
            }
        }
    }
}

SCENARIO( "AnsiSgrParser true color (38;2;r;g;b)", "[ansi]" )
{
    GIVEN( "A line with true color foreground" )
    {
        const auto line = QStringLiteral( "\033[38;2;255;128;0mTrueColor" );

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "text uses specified RGB" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "TrueColor" ) );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == QColor( 255, 128, 0 ) );
            }
        }
    }

    GIVEN( "True color mixed with reset" )
    {
        const auto line = QStringLiteral( "\033[38;2;100;200;50mCustom\033[0m default" );

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "custom color segment followed by default" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Custom default" ) );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == QColor( 100, 200, 50 ) );
            }
        }
    }
}

SCENARIO( "AnsiSgrParser StripOnly mode", "[ansi]" )
{
    GIVEN( "A line with ANSI color codes" )
    {
        const auto line = QStringLiteral( "\033[31mRed\033[0m" );

        WHEN( "mode is StripOnly" )
        {
            const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::StripOnly,
                                                          kDefaultFg, kDefaultBg );

            THEN( "codes removed, no segments produced" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Red" ) );
                REQUIRE( result.segments.empty() );
            }
        }
    }

    GIVEN( "Complex line in StripOnly mode" )
    {
        const auto line = QStringLiteral( "Hello \033[38;5;196mWorld\033[0m!" );

        WHEN( "parsed in StripOnly mode" )
        {
            const auto result = AnsiSgrParser::parseLine( line, AnsiProcessing::StripOnly,
                                                          kDefaultFg, kDefaultBg );

            THEN( "all codes removed, no segments" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Hello World!" ) );
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
            const auto line = QStringLiteral( "\033[2KHello" );
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "CSI is stripped, no color segment" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Hello" ) );
                REQUIRE( result.segments.empty() );
            }
        }

        WHEN( "CSI H (cursor position)" )
        {
            const auto line = QStringLiteral( "\033[1;1HStart" );
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "CSI is stripped" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Start" ) );
                REQUIRE( result.segments.empty() );
            }
        }

        WHEN( "Mixed SGR and non-SGR CSI" )
        {
            const auto line = QStringLiteral( "\033[2K\033[31mRed" );
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "non-SGR stripped, SGR applied" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Red" ) );
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
        const QString line;

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "returns empty clean text" )
            {
                REQUIRE( result.cleanText.isEmpty() );
                REQUIRE( result.segments.empty() );
            }
        }
    }

    GIVEN( "A line with only ANSI codes" )
    {
        const auto line = QStringLiteral( "\033[31m\033[32m\033[0m" );

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "clean text is empty" )
            {
                REQUIRE( result.cleanText.isEmpty() );
                REQUIRE( result.segments.empty() );
            }
        }
    }

    GIVEN( "ANSI code at start with color spanning to end" )
    {
        const auto line = QStringLiteral( "\033[31mRed no reset" );

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "entire text is red" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Red no reset" ) );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
                REQUIRE( result.segments[ 0 ].startColumn == 0 );
                REQUIRE( result.segments[ 0 ].length == 12 );
            }
        }
    }

    GIVEN( "Default foreground reset (39)" )
    {
        const auto line = QStringLiteral( "\033[31mRed\033[39m default" );

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "39 resets to default foreground" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "Red default" ) );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
            }
        }
    }

    GIVEN( "Bold foreground (1;31)" )
    {
        const auto line = QStringLiteral( "\033[1;31mBoldRed" );

        WHEN( "parsed in RenderColors mode" )
        {
            const auto result
                = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors, kDefaultFg,
                                            kDefaultBg );

            THEN( "bold is ignored, color applied" )
            {
                REQUIRE( result.cleanText == QStringLiteral( "BoldRed" ) );
                REQUIRE( result.segments.size() == 1 );
                REQUIRE( result.segments[ 0 ].foreColor == kRed );
            }
        }
    }
}
