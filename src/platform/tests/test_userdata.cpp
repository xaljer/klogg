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

#include <filesystem>
#include <fstream>

#include "userdata.h"

namespace fs = std::filesystem;

TEST_CASE( "userdata maps kinds to file names", "[userdata]" )
{
    REQUIRE( std::string( platform::userdata::fileName( platform::userdata::Kind::Config ) )
             == "config.toml" );
    REQUIRE( std::string( platform::userdata::fileName( platform::userdata::Kind::Session ) )
             == "session.toml" );
}

TEST_CASE( "userdata adopts an interim document name once", "[userdata]" )
{
    const auto dir = fs::temp_directory_path() / "klogg_userdata_test";
    fs::remove_all( dir );
    fs::create_directories( dir );

    std::ofstream( dir / "klogg.toml" ) << "[perf]\n";

    REQUIRE( platform::userdata::adoptLegacyName( dir, "klogg.toml", "config.toml" ) );
    REQUIRE( fs::exists( dir / "config.toml" ) );
    REQUIRE_FALSE( fs::exists( dir / "klogg.toml" ) );

    // Idempotent: nothing to adopt the second time.
    REQUIRE_FALSE( platform::userdata::adoptLegacyName( dir, "klogg.toml", "config.toml" ) );

    fs::remove_all( dir );
}

TEST_CASE( "userdata does not clobber an existing new-name document", "[userdata]" )
{
    const auto dir = fs::temp_directory_path() / "klogg_userdata_test2";
    fs::remove_all( dir );
    fs::create_directories( dir );

    std::ofstream( dir / "klogg.toml" ) << "[old]\n";
    std::ofstream( dir / "config.toml" ) << "[new]\n";

    REQUIRE_FALSE( platform::userdata::adoptLegacyName( dir, "klogg.toml", "config.toml" ) );
    REQUIRE( fs::exists( dir / "klogg.toml" ) );

    fs::remove_all( dir );
}
