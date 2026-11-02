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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <simdutf.h>

#include "wrap.h"

namespace klogg::render {

namespace {

constexpr char16_t ReplacementCharacter = 0xfffd;

// The QChar::isSpace() set (Unicode White_Space, all of it inside the BMP) that
// decided break points in the Qt column wrapper.
constexpr bool isSpace( char16_t c )
{
    return ( c < 0x100 && ( c == 0x20 || ( c > 0x8 && c < 0xe ) || c == 0x85 || c == 0xa0 ) )
           || ( c >= 0x2000 && c <= 0x200a ) || c == 0x1680 || c == 0x2028 || c == 0x2029
           || c == 0x202f || c == 0x205f || c == 0x3000;
}

// Fallback for input simdutf rejects: one replacement character per malformed
// byte, so a line with a broken sequence keeps a stable, documented column
// count instead of collapsing into a single empty part.
std::u16string toUtf16Fallback( std::string_view utf8 )
{
    std::u16string result;
    result.reserve( utf8.size() );

    std::size_t position = 0;
    while ( position < utf8.size() ) {
        const auto lead = static_cast<unsigned char>( utf8[ position ] );
        std::size_t sequenceLength = 0;
        std::uint32_t codePoint = 0;

        if ( lead < 0x80 ) {
            sequenceLength = 1;
            codePoint = lead;
        }
        else if ( ( lead & 0xe0 ) == 0xc0 ) {
            sequenceLength = 2;
            codePoint = lead & 0x1f;
        }
        else if ( ( lead & 0xf0 ) == 0xe0 ) {
            sequenceLength = 3;
            codePoint = lead & 0x0f;
        }
        else if ( ( lead & 0xf8 ) == 0xf0 ) {
            sequenceLength = 4;
            codePoint = lead & 0x07;
        }

        bool valid = sequenceLength > 0 && position + sequenceLength <= utf8.size();
        for ( std::size_t i = 1; valid && i < sequenceLength; ++i ) {
            const auto continuation = static_cast<unsigned char>( utf8[ position + i ] );
            valid = ( continuation & 0xc0 ) == 0x80;
            codePoint = ( codePoint << 6 ) | ( continuation & 0x3f );
        }

        if ( valid ) {
            std::uint32_t minCodePoint = 0;
            if ( sequenceLength == 2 ) {
                minCodePoint = 0x80u;
            }
            else if ( sequenceLength == 3 ) {
                minCodePoint = 0x800u;
            }
            else if ( sequenceLength == 4 ) {
                minCodePoint = 0x10000u;
            }

            valid = codePoint >= minCodePoint && codePoint <= 0x10ffff
                    && !( codePoint >= 0xd800 && codePoint <= 0xdfff );
        }

        if ( !valid ) {
            result.push_back( ReplacementCharacter );
            ++position;
            continue;
        }

        if ( codePoint < 0x10000 ) {
            result.push_back( static_cast<char16_t>( codePoint ) );
        }
        else {
            const auto surrogateOffset = codePoint - 0x10000u;
            result.push_back( static_cast<char16_t>( 0xd800u + ( surrogateOffset >> 10 ) ) );
            result.push_back( static_cast<char16_t>( 0xdc00u + ( surrogateOffset & 0x3ffu ) ) );
        }
        position += sequenceLength;
    }

    return result;
}

// Column arithmetic is on UTF-16 code units, matching the QStringView::size()
// columns the Qt implementation used and the columns HighlighterSet reports.
std::u16string toUtf16( std::string_view utf8 )
{
    if ( utf8.empty() ) {
        return {};
    }

    // utf16_length_from_utf8 does not validate its input, so clamp the
    // (implementation defined for broken input) count to a bound that holds for
    // any valid encoding of this many bytes: 4 bytes and 2 units per code point.
    const auto maxUtf16Length = 2 * utf8.size();
    const auto utf16Length
        = std::min( simdutf::utf16_length_from_utf8( utf8.data(), utf8.size() ), maxUtf16Length );
    std::u16string result( utf16Length, u'\0' );
    const auto written = simdutf::convert_utf8_to_utf16( utf8.data(), utf8.size(), result.data() );
    if ( written == 0 ) {
        return toUtf16Fallback( utf8 );
    }

    result.resize( written );
    return result;
}

WrapPart makePart( std::size_t index, std::size_t start, std::size_t length )
{
    return WrapPart{ LineNumber{ static_cast<LineNumber::UnderlyingType>( index ) },
                     LineColumn{ static_cast<LineColumn::UnderlyingType>( start ) },
                     LineLength{ static_cast<LineLength::UnderlyingType>( length ) } };
}

} // namespace

std::vector<WrapPart> wrapLine( std::string_view utf8Text, LineColumn wrapColumns )
{
    const auto text = toUtf16( utf8Text );
    std::vector<WrapPart> parts;

    if ( text.empty() ) {
        parts.push_back( makePart( 0, 0, 0 ) );
        return parts;
    }

    const auto totalColumns = text.size();
    if ( wrapColumns.get() <= 0 ) {
        parts.push_back( makePart( 0, 0, totalColumns ) );
        return parts;
    }

    const auto wrapWidth = static_cast<std::size_t>( wrapColumns.get() );
    parts.reserve( 1 + ( totalColumns / wrapWidth ) );

    std::size_t start = 0;
    while ( totalColumns - start > wrapWidth ) {
        std::size_t cut = 0;
        for ( auto column = start + wrapWidth; column > start; --column ) {
            if ( isSpace( text[ column - 1 ] ) ) {
                cut = column - start;
                break;
            }
        }
        if ( cut == 0 ) {
            cut = wrapWidth;
        }

        parts.push_back( makePart( parts.size(), start, cut ) );
        start += cut;
    }

    if ( totalColumns > start ) {
        parts.push_back( makePart( parts.size(), start, totalColumns - start ) );
    }

    return parts;
}

} // namespace klogg::render
