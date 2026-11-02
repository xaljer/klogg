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

#include "ansi_parser.h"

#include <algorithm>
#include <array>

namespace {

// Returns true if c is a CSI terminator (sequences end with a letter)
bool isCsiTerminator( unsigned char c )
{
    return ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' );
}

// Segment columns are UTF-16 code units of the display text, the space
// LineColumn and QString::mid() use: one unit per code point, two for the four
// byte forms that stand for a surrogate pair.
bool isUtf8ContinuationByte( unsigned char byte )
{
    return ( byte & 0xC0 ) == 0x80;
}

int utf16WidthOfUtf8LeadByte( unsigned char byte )
{
    return byte >= 0xF0 && byte <= 0xF7 ? 2 : 1;
}

const std::array<klogg::Rgba, 16>& standardColorTable()
{
    static const std::array<klogg::Rgba, 16> table = {
        klogg::Rgba( 0, 0, 0 ),       // 0: Black
        klogg::Rgba( 205, 0, 0 ),     // 1: Red
        klogg::Rgba( 0, 205, 0 ),     // 2: Green
        klogg::Rgba( 205, 205, 0 ),   // 3: Yellow
        klogg::Rgba( 0, 0, 238 ),     // 4: Blue
        klogg::Rgba( 205, 0, 205 ),   // 5: Magenta
        klogg::Rgba( 0, 205, 205 ),   // 6: Cyan
        klogg::Rgba( 229, 229, 229 ), // 7: White
        klogg::Rgba( 127, 127, 127 ), // 8: Bright Black
        klogg::Rgba( 255, 0, 0 ),     // 9: Bright Red
        klogg::Rgba( 0, 255, 0 ),     // 10: Bright Green
        klogg::Rgba( 255, 255, 0 ),   // 11: Bright Yellow
        klogg::Rgba( 92, 92, 255 ),   // 12: Bright Blue
        klogg::Rgba( 255, 0, 255 ),   // 13: Bright Magenta
        klogg::Rgba( 0, 255, 255 ),   // 14: Bright Cyan
        klogg::Rgba( 255, 255, 255 ), // 15: Bright White
    };
    return table;
}

} // namespace

klogg::Rgba AnsiSgrParser::standardColor( int index )
{
    if ( index >= 0 && index < 16 ) {
        return standardColorTable()[ static_cast<size_t>( index ) ];
    }
    return {};
}

klogg::Rgba AnsiSgrParser::colorFrom256( int index )
{
    if ( index < 0 || index > 255 ) {
        return {};
    }
    if ( index < 16 ) {
        return standardColor( index );
    }
    if ( index >= 232 ) {
        const int gray = ( index - 232 ) * 10 + 8;
        return klogg::Rgba( static_cast<uint8_t>( gray ), static_cast<uint8_t>( gray ),
                            static_cast<uint8_t>( gray ) );
    }
    const int n = index - 16;
    const int r = ( n / 36 ) * 51;
    const int g = ( ( n / 6 ) % 6 ) * 51;
    const int b = ( n % 6 ) * 51;
    return klogg::Rgba( static_cast<uint8_t>( r ), static_cast<uint8_t>( g ),
                        static_cast<uint8_t>( b ) );
}

void AnsiSgrParser::AnsiState::reset()
{
    fgIndex = -1;
    hasFgTrueColor = false;
    bgIndex = -1;
}

void AnsiSgrParser::AnsiState::applyCommand( int cmd )
{
    switch ( cmd ) {
    case 0:
        reset();
        break;
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
        fgIndex = cmd - 30;
        hasFgTrueColor = false;
        break;
    case 39:
        fgIndex = -1;
        hasFgTrueColor = false;
        break;
    case 40:
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
        bgIndex = cmd - 40;
        break;
    case 49:
        bgIndex = -1;
        break;
    case 90:
    case 91:
    case 92:
    case 93:
    case 94:
    case 95:
    case 96:
    case 97:
        fgIndex = cmd - 90 + 8;
        hasFgTrueColor = false;
        break;
    case 100:
    case 101:
    case 102:
    case 103:
    case 104:
    case 105:
    case 106:
    case 107:
        bgIndex = cmd - 100 + 8;
        break;
    default:
        break;
    }
}

klogg::Rgba AnsiSgrParser::AnsiState::resolveForeground( const klogg::Rgba& defaultFg ) const
{
    if ( hasFgTrueColor ) {
        return fgTrueColor;
    }
    if ( fgIndex >= 0 ) {
        return standardColor( fgIndex );
    }
    return defaultFg;
}

AnsiParseResult AnsiSgrParser::parseLine( const std::string& line, AnsiProcessing mode,
                                          const klogg::Rgba& defaultFg, const klogg::Rgba& defaultBg )
{
    (void)defaultBg;

    if ( mode == AnsiProcessing::None || line.find( '\x1b' ) == std::string::npos ) {
        return { line, {} };
    }

    const bool collectSegments = ( mode == AnsiProcessing::RenderColors );

    AnsiState state;
    std::string cleanText;
    cleanText.reserve( line.size() );
    klogg::vector<AnsiSegment> segments;

    int textStart = 0;
    int cleanColumns = 0;
    klogg::Rgba currentFg = defaultFg;
    std::size_t rawIdx = 0;
    const std::size_t rawLen = line.size();

    auto closeRun = [ & ]( int cleanEnd ) {
        const int runLen = cleanEnd - textStart;
        if ( runLen > 0 && collectSegments && currentFg != defaultFg ) {
            segments.push_back( { textStart, runLen, currentFg } );
        }
    };

    while ( rawIdx < rawLen ) {
        if ( static_cast<unsigned char>( line[ rawIdx ] ) == 0x1B ) {
            closeRun( cleanColumns );

            ++rawIdx;
            if ( rawIdx < rawLen && line[ rawIdx ] == '[' ) {
                ++rawIdx;
                klogg::vector<int> params;
                int currentParam = -1;
                unsigned char terminator = 0;

                while ( rawIdx < rawLen ) {
                    const unsigned char ch = static_cast<unsigned char>( line[ rawIdx ] );
                    ++rawIdx;

                    if ( isCsiTerminator( ch ) ) {
                        terminator = ch;
                        break;
                    }

                    if ( ch >= '0' && ch <= '9' ) {
                        if ( currentParam < 0 ) {
                            currentParam = 0;
                        }
                        currentParam = currentParam * 10 + ( ch - '0' );
                    }
                    else if ( ch == ';' || ch == ':' ) {
                        params.push_back( currentParam >= 0 ? currentParam : 0 );
                        currentParam = -1;
                    }
                }
                if ( currentParam >= 0 ) {
                    params.push_back( currentParam );
                }

                if ( terminator == 'm' ) {
                    if ( params.empty() ) {
                        params.push_back( 0 );
                    }

                    size_t pi = 0;
                    while ( pi < params.size() ) {
                        const int cmd = params[ pi ];
                        if ( cmd == 38 || cmd == 48 ) {
                            if ( pi + 2 < params.size() && params[ pi + 1 ] == 5 ) {
                                const int colorIdx = params[ pi + 2 ];
                                if ( cmd == 38 ) {
                                    state.fgIndex = -1;
                                    state.hasFgTrueColor = true;
                                    state.fgTrueColor = colorFrom256( colorIdx );
                                }
                                else {
                                    state.bgIndex = -1;
                                }
                                pi += 3;
                                continue;
                            }
                            if ( pi + 4 < params.size() && params[ pi + 1 ] == 2 ) {
                                if ( cmd == 38 ) {
                                    state.fgIndex = -1;
                                    state.hasFgTrueColor = true;
                                    state.fgTrueColor = klogg::Rgba(
                                        static_cast<uint8_t>(
                                            std::clamp( params[ pi + 2 ], 0, 255 ) ),
                                        static_cast<uint8_t>(
                                            std::clamp( params[ pi + 3 ], 0, 255 ) ),
                                        static_cast<uint8_t>(
                                            std::clamp( params[ pi + 4 ], 0, 255 ) ) );
                                }
                                else {
                                    state.bgIndex = -1;
                                }
                                pi += 5;
                                continue;
                            }
                        }

                        state.applyCommand( cmd );
                        ++pi;
                    }

                    currentFg = state.resolveForeground( defaultFg );
                }
            }
            else {
                while ( rawIdx < rawLen
                        && !isCsiTerminator( static_cast<unsigned char>( line[ rawIdx ] ) )
                        && static_cast<unsigned char>( line[ rawIdx ] ) != 0x1B ) {
                    ++rawIdx;
                }
                if ( rawIdx < rawLen
                     && isCsiTerminator( static_cast<unsigned char>( line[ rawIdx ] ) ) ) {
                    ++rawIdx;
                }
            }

            textStart = cleanColumns;
        }
        else {
            const auto byte = static_cast<unsigned char>( line[ rawIdx ] );
            cleanText.push_back( line[ rawIdx ] );
            if ( !isUtf8ContinuationByte( byte ) ) {
                cleanColumns += utf16WidthOfUtf8LeadByte( byte );
            }
            ++rawIdx;
        }
    }

    closeRun( cleanColumns );

    return { std::move( cleanText ), std::move( segments ) };
}

AnsiDisplayLine AnsiSgrParser::processDisplayLine( std::string expandedLine, AnsiProcessing mode,
                                                   const klogg::Rgba& defaultFg,
                                                   const klogg::Rgba& defaultBg )
{
    if ( mode == AnsiProcessing::None || expandedLine.find( '\x1b' ) == std::string::npos ) {
        return { std::move( expandedLine ), {} };
    }

    auto result = parseLine( expandedLine, mode, defaultFg, defaultBg );
    return { std::move( result.cleanText ), std::move( result.segments ) };
}
