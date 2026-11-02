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

#include <string>
#include <string_view>

#include "string_utils.h"

using klogg::ascii_iequals;
using klogg::ascii_to_lower;
using klogg::utf8_codepoint_count;
using klogg::utf8_next;
using klogg::utf8_sequence_length;

TEST_CASE( "ascii_to_lower folds ASCII only" )
{
    REQUIRE( klogg::ascii_to_lower( 'A' ) == 'a' );
    REQUIRE( klogg::ascii_to_lower( 'Z' ) == 'z' );
    REQUIRE( klogg::ascii_to_lower( '0' ) == '0' );
}

TEST_CASE( "ascii_to_lower normalises whole strings" )
{
    REQUIRE( ascii_to_lower( "HeLLo" ) == "hello" );
    REQUIRE( ascii_to_lower( "" ) == "" );
}

TEST_CASE( "ascii_iequals is case-insensitive for ASCII" )
{
    REQUIRE( ascii_iequals( "Hello", "HELLO" ) );
    REQUIRE( ascii_iequals( "", "" ) );
    REQUIRE_FALSE( ascii_iequals( "Hello", "World" ) );
    REQUIRE_FALSE( ascii_iequals( "Hello", "Hell" ) );
    REQUIRE_FALSE( ascii_iequals( "", "x" ) );
}

TEST_CASE( "ascii_iequals keeps non-ASCII bytes distinct" )
{
    // "中" (E4 B8 AD) and "国" (E5 9B BD) differ in their first byte.
    REQUIRE_FALSE( ascii_iequals( "\xE4\xB8\xAD", "\xE5\x9B\xBD" ) );
}

TEST_CASE( "utf8_sequence_length classifies lead bytes" )
{
    REQUIRE( utf8_sequence_length( 0x00 ) == 1 );
    REQUIRE( utf8_sequence_length( 0x7F ) == 1 );
    REQUIRE( utf8_sequence_length( 0xC0 ) == 2 );
    REQUIRE( utf8_sequence_length( 0xDF ) == 2 );
    REQUIRE( utf8_sequence_length( 0xE0 ) == 3 );
    REQUIRE( utf8_sequence_length( 0xEF ) == 3 );
    REQUIRE( utf8_sequence_length( 0xF0 ) == 4 );
    REQUIRE( utf8_sequence_length( 0xF7 ) == 4 );
    // Continuation byte and invalid lead are treated as a single byte.
    REQUIRE( utf8_sequence_length( 0x80 ) == 1 );
    REQUIRE( utf8_sequence_length( 0xFF ) == 1 );
}

TEST_CASE( "utf8_codepoint_count counts multi-byte sequences as one" )
{
    REQUIRE( utf8_codepoint_count( "" ) == 0 );
    REQUIRE( utf8_codepoint_count( "abc" ) == 3 );
    // "中" is a 3-byte character.
    REQUIRE( utf8_codepoint_count( "\xE4\xB8\xAD" ) == 1 );
    REQUIRE( utf8_codepoint_count( "A\xE4\xB8\xAD" ) == 2 );
}

TEST_CASE( "utf8_next advances one code point" )
{
    const std::string s = "A\xE4\xB8\xAD";
    REQUIRE( utf8_next( s, 0 ) == 1 );
    REQUIRE( utf8_next( s, 1 ) == 4 );
    REQUIRE( utf8_next( s, 4 ) == 4 ); // past end clamps to size
}
