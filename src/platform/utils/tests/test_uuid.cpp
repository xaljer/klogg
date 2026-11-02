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
#include <array>
#include <string>

#include "uuid.h"

TEST_CASE( "generateIdFromUuid produces the standard dashed lowercase form" )
{
    const auto id = generateIdFromUuid();

    REQUIRE( id.size() == 36 );
    REQUIRE( id[ 8 ] == '-' );
    REQUIRE( id[ 13 ] == '-' );
    REQUIRE( id[ 18 ] == '-' );
    REQUIRE( id[ 23 ] == '-' );

    for ( const auto c : id ) {
        const bool isDash = c == '-';
        const bool isHexDigit = ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' );
        REQUIRE( ( isDash || isHexDigit ) );
    }
}

TEST_CASE( "generateIdFromUuid does not repeat" )
{
    std::array<std::string, 1000> ids;
    for ( auto& id : ids ) {
        id = generateIdFromUuid();
    }

    std::sort( ids.begin(), ids.end() );
    REQUIRE( std::adjacent_find( ids.begin(), ids.end() ) == ids.end() );
}
