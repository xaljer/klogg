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

#ifndef KLOGG_STRING_UTILS_H
#define KLOGG_STRING_UTILS_H

#include <cstddef>
#include <string>
#include <string_view>

namespace klogg {

// ASCII-only case folding that leaves non-ASCII bytes untouched, so it is safe
// on UTF-8 text without needing a full Unicode case table. Used for comparing
// keys, colours, keywords and file extensions. Full-text search case handling
// is left to the regex engine.
constexpr char ascii_to_lower( char c )
{
    return ( c >= 'A' && c <= 'Z' ) ? static_cast<char>( c + ( 'a' - 'A' ) ) : c;
}

inline std::string ascii_to_lower( std::string_view s )
{
    std::string result( s.size(), '\0' );
    for ( std::size_t i = 0; i < s.size(); ++i ) {
        result[ i ] = ascii_to_lower( s[ i ] );
    }
    return result;
}

inline bool ascii_iequals( std::string_view a, std::string_view b )
{
    if ( a.size() != b.size() ) {
        return false;
    }
    for ( std::size_t i = 0; i < a.size(); ++i ) {
        if ( ascii_to_lower( a[ i ] ) != ascii_to_lower( b[ i ] ) ) {
            return false;
        }
    }
    return true;
}

// Byte length of the UTF-8 sequence starting with lead. Continuation bytes and
// invalid lead bytes (0x80-0xBF, 0xF8-0xFF) are reported as a single byte so a
// well-formed sequence is never split and malformed input degrades gracefully.
inline std::size_t utf8_sequence_length( unsigned char lead )
{
    if ( lead < 0x80 ) {
        return 1;
    }
    if ( ( lead & 0xE0 ) == 0xC0 ) {
        return 2;
    }
    if ( ( lead & 0xF0 ) == 0xE0 ) {
        return 3;
    }
    if ( ( lead & 0xF8 ) == 0xF0 ) {
        return 4;
    }
    return 1;
}

// Number of Unicode code points in s. Only the lead byte of each sequence is
// consulted, so a trailing truncated sequence counts as one code point.
inline std::size_t utf8_codepoint_count( std::string_view s )
{
    std::size_t count = 0;
    std::size_t pos = 0;
    const std::size_t size = s.size();
    while ( pos < size ) {
        ++count;
        pos += utf8_sequence_length( static_cast<unsigned char>( s[ pos ] ) );
    }
    return count;
}

// Position of the code point following the one starting at pos. May return a
// value past s.size() only for a malformed trailing sequence; clamp with
// std::min if a caller needs an in-bounds result.
inline std::size_t utf8_next( std::string_view s, std::size_t pos )
{
    if ( pos >= s.size() ) {
        return s.size();
    }
    return pos + utf8_sequence_length( static_cast<unsigned char>( s[ pos ] ) );
}

} // namespace klogg

#endif // KLOGG_STRING_UTILS_H
