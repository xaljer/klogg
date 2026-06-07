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

#include <array>

namespace {

// Returns true if c is a CSI terminator (sequences end with a letter)
bool isCsiTerminator( QChar c )
{
    const char16_t ch = c.unicode();
    return ( ch >= 'A' && ch <= 'Z' ) || ( ch >= 'a' && ch <= 'z' );
}

const std::array<QColor, 16>& standardColorTable()
{
    static const std::array<QColor, 16> table = {
        QColor( 0, 0, 0 ),       // 0: Black
        QColor( 205, 0, 0 ),     // 1: Red
        QColor( 0, 205, 0 ),     // 2: Green
        QColor( 205, 205, 0 ),   // 3: Yellow
        QColor( 0, 0, 238 ),     // 4: Blue
        QColor( 205, 0, 205 ),   // 5: Magenta
        QColor( 0, 205, 205 ),   // 6: Cyan
        QColor( 229, 229, 229 ), // 7: White
        QColor( 127, 127, 127 ), // 8: Bright Black
        QColor( 255, 0, 0 ),     // 9: Bright Red
        QColor( 0, 255, 0 ),     // 10: Bright Green
        QColor( 255, 255, 0 ),   // 11: Bright Yellow
        QColor( 92, 92, 255 ),   // 12: Bright Blue
        QColor( 255, 0, 255 ),   // 13: Bright Magenta
        QColor( 0, 255, 255 ),   // 14: Bright Cyan
        QColor( 255, 255, 255 ), // 15: Bright White
    };
    return table;
}

} // namespace

QColor AnsiSgrParser::standardColor( int index )
{
    if ( index >= 0 && index < 16 ) {
        return standardColorTable()[ static_cast<size_t>( index ) ];
    }
    return {};
}

QColor AnsiSgrParser::colorFrom256( int index )
{
    if ( index < 0 || index > 255 ) {
        return {};
    }
    if ( index < 16 ) {
        return standardColor( index );
    }
    if ( index >= 232 ) {
        const int gray = ( index - 232 ) * 10 + 8;
        return QColor( gray, gray, gray );
    }
    const int n = index - 16;
    const int r = ( n / 36 ) * 51;
    const int g = ( ( n / 6 ) % 6 ) * 51;
    const int b = ( n % 6 ) * 51;
    return QColor( r, g, b );
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

QColor AnsiSgrParser::AnsiState::resolveForeground( const QColor& defaultFg ) const
{
    if ( hasFgTrueColor ) {
        return fgTrueColor;
    }
    if ( fgIndex >= 0 ) {
        return standardColor( fgIndex );
    }
    return defaultFg;
}

AnsiParseResult AnsiSgrParser::parseLine( const QString& line, AnsiProcessing mode,
                                           const QColor& defaultFg, const QColor& defaultBg )
{
    Q_UNUSED( defaultBg );

    if ( mode == AnsiProcessing::None || !line.contains( QChar( 0x1B ) ) ) {
        return { line, {} };
    }

    const bool collectSegments = ( mode == AnsiProcessing::RenderColors );

    AnsiState state;
    QString cleanText;
    cleanText.reserve( line.size() );
    klogg::vector<AnsiSegment> segments;

    int textStart = 0;
    QColor currentFg = defaultFg;
    int rawIdx = 0;
    const int rawLen = static_cast<int>( line.size() );

    auto closeRun = [ & ]( int cleanEnd ) {
        const int runLen = cleanEnd - textStart;
        if ( runLen > 0 && collectSegments && currentFg != defaultFg ) {
            segments.push_back( { textStart, runLen, currentFg } );
        }
    };

    while ( rawIdx < rawLen ) {
        if ( line[ rawIdx ].unicode() == 0x1B ) {
            closeRun( static_cast<int>( cleanText.size() ) );

            ++rawIdx;
            if ( rawIdx < rawLen && line[ rawIdx ] == QLatin1Char( '[' ) ) {
                ++rawIdx;
                klogg::vector<int> params;
                int currentParam = -1;
                QChar terminator;

                while ( rawIdx < rawLen ) {
                    const QChar ch = line[ rawIdx ];
                    ++rawIdx;

                    if ( isCsiTerminator( ch ) ) {
                        terminator = ch;
                        break;
                    }

                    const char16_t cu = ch.unicode();
                    if ( cu >= '0' && cu <= '9' ) {
                        if ( currentParam < 0 ) {
                            currentParam = 0;
                        }
                        currentParam = currentParam * 10 + ( cu - '0' );
                    }
                    else if ( cu == ';' || cu == ':' ) {
                        params.push_back( currentParam >= 0 ? currentParam : 0 );
                        currentParam = -1;
                    }
                }
                if ( currentParam >= 0 ) {
                    params.push_back( currentParam );
                }

                if ( terminator == QLatin1Char( 'm' ) ) {
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
                                    state.fgTrueColor = QColor( params[ pi + 2 ], params[ pi + 3 ],
                                                                 params[ pi + 4 ] );
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
                while ( rawIdx < rawLen && !isCsiTerminator( line[ rawIdx ] )
                        && line[ rawIdx ].unicode() != 0x1B ) {
                    ++rawIdx;
                }
                if ( rawIdx < rawLen && isCsiTerminator( line[ rawIdx ] ) ) {
                    ++rawIdx;
                }
            }

            textStart = static_cast<int>( cleanText.size() );
        }
        else {
            cleanText.append( line[ rawIdx ] );
            ++rawIdx;
        }
    }

    closeRun( static_cast<int>( cleanText.size() ) );

    return { std::move( cleanText ), std::move( segments ) };
}
