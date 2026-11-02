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
#include <string>
#include <system_error>

#include "paths.h"
#include "tempdir.h"

namespace fs = std::filesystem;

namespace {

// UTF-8 generic format (forward slashes on every platform) matches the
// resolveTempRoot output; generic_u8string() is char8_t under C++20, rewrap
// without locale conversion.
std::string pathToUtf8( const fs::path& path )
{
    const auto u8 = path.generic_u8string();
    return std::string( reinterpret_cast<const char*>( u8.data() ), u8.size() );
}

// Temp directory under the system temp, removed on scope exit.
struct ScopedTempDir {
    fs::path path;

    ScopedTempDir()
    {
        std::error_code ec;
        path = fs::temp_directory_path() / "klogg_utils_tempdir_test";
        fs::remove_all( path, ec );
        fs::create_directories( path, ec );
    }

    ~ScopedTempDir()
    {
        std::error_code ec;
        fs::remove_all( path, ec );
    }
};

} // namespace

TEST_CASE( "resolveTempRoot uses the system temp when custom is empty" )
{
    const std::string systemTemp = pathToUtf8( fs::temp_directory_path() );

    const std::string root = klogg::resolveTempRoot( std::string{}, systemTemp );

    REQUIRE( root == pathToUtf8( fs::temp_directory_path() / "klogg_temp" ) );
    REQUIRE( fs::is_directory( platform::paths::pathFromUtf8( root ) ) );
}

TEST_CASE( "resolveTempRoot uses the custom dir when it is writable" )
{
    ScopedTempDir customParent;
    const std::string systemTemp = pathToUtf8( fs::temp_directory_path() );

    const std::string root = klogg::resolveTempRoot( pathToUtf8( customParent.path ), systemTemp );

    REQUIRE( root == pathToUtf8( customParent.path / "klogg_temp" ) );
    REQUIRE( fs::is_directory( platform::paths::pathFromUtf8( root ) ) );
}

TEST_CASE( "resolveTempRoot falls back when the custom dir is unusable" )
{
    ScopedTempDir holder;
    const std::string systemTemp = pathToUtf8( fs::temp_directory_path() );

    // A path *under* a regular file: create_directories must fail on every platform.
    const fs::path filePath = holder.path / "a_file";
    std::ofstream( filePath ).put( 'x' );
    const std::string badCustom = pathToUtf8( filePath / "sub" );

    const std::string root = klogg::resolveTempRoot( badCustom, systemTemp );

    REQUIRE( root == pathToUtf8( fs::temp_directory_path() / "klogg_temp" ) );
    REQUIRE( fs::is_directory( platform::paths::pathFromUtf8( root ) ) );
}
