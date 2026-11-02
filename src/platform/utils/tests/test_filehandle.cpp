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
#include <vector>

#include "filehandle.h"

using klogg::FileHandle;
namespace fs = std::filesystem;

namespace {

const char* const kContent = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr std::size_t kContentLength = 36;

// Temp ASCII-named file with known single-byte content, removed on scope exit.
// The name is ASCII so open(path) round-trips the path as UTF-8 on every host.
struct TempTestFile {
    fs::path path;

    TempTestFile()
    {
        path = fs::temp_directory_path() / "klogg_filehandle_test.tmp";
        std::error_code ec;
        fs::remove( path, ec );
        std::ofstream out( path, std::ios::binary );
        out.write( kContent, kContentLength );
    }

    ~TempTestFile()
    {
        std::error_code ec;
        fs::remove( path, ec );
    }
};

} // namespace

TEST_CASE( "FileHandle opens, reports size and reads at an offset" )
{
    TempTestFile file;
    FileHandle handle;

    REQUIRE_FALSE( handle.isOpen() );
    REQUIRE( handle.open( file.path.string() ) );
    REQUIRE( handle.isOpen() );
    REQUIRE( handle.size() == kContentLength );

    std::vector<char> buffer( 16 );
    REQUIRE( handle.readAt( 0, buffer.data(), 10 ) == 10 );
    REQUIRE( std::string( buffer.data(), 10 ) == "0123456789" );

    REQUIRE( handle.readAt( 5, buffer.data(), 5 ) == 5 );
    REQUIRE( std::string( buffer.data(), 5 ) == "56789" );

    handle.close();
    REQUIRE_FALSE( handle.isOpen() );
    REQUIRE_FALSE( handle.size().has_value() );
    REQUIRE( handle.readAt( 0, buffer.data(), 10 ) == 0 );
}

TEST_CASE( "FileHandle returns zero at and past EOF" )
{
    TempTestFile file;
    FileHandle handle;
    REQUIRE( handle.open( file.path.string() ) );

    std::vector<char> buffer( 8 );
    REQUIRE( handle.readAt( kContentLength, buffer.data(), buffer.size() ) == 0 );
    REQUIRE( handle.readAt( kContentLength + 100, buffer.data(), buffer.size() ) == 0 );
}

TEST_CASE( "FileHandle sequential scan concatenates to the whole file" )
{
    TempTestFile file;
    FileHandle handle;
    REQUIRE( handle.open( file.path.string() ) );

    std::string collected;
    std::vector<char> block( 7 );
    for ( uint64_t offset = 0; offset < kContentLength; ) {
        const size_t read = handle.readAt( offset, block.data(), block.size() );
        REQUIRE( read > 0 );
        collected.append( block.data(), read );
        offset += read;
    }
    REQUIRE( collected == std::string( kContent, kContentLength ) );
}
