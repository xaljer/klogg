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

#include "crc32.h"

TEST_CASE( "Crc32 matches zlib reference values" )
{
    REQUIRE( Crc32::calculate( "" ) == 0x00000000 );
    REQUIRE( Crc32::calculate( "A" ) == 0xd3d99e8b );
    REQUIRE( Crc32::calculate( "abc" ) == 0x352441c2 );
    REQUIRE( Crc32::calculate( "123456789" ) == 0xcbf43926 );
    REQUIRE( Crc32::calculate( "The quick brown fox jumps over the lazy dog" ) == 0x414fa339 );
}

TEST_CASE( "Crc32 hashes raw bytes including non-ASCII and 0xff" )
{
    REQUIRE( Crc32::calculate( std::string( "\xff", 1 ) ) == 0xff000000 );

    // "中" as UTF-8 bytes: hex-escaped so the test does not depend on source encoding.
    REQUIRE( Crc32::calculate( std::string_view( "\xe4\xb8\xad", 3 ) ) == 0xd5d15a8b );
}
