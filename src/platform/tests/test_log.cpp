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
#include <optional>
#include <sstream>
#include <string>

#include "platform_log.h"

namespace {

std::filesystem::path tempLogFile( const std::string& name )
{
    auto path = std::filesystem::temp_directory_path() / name;
    std::error_code error;
    std::filesystem::remove( path, error );
    return path;
}

std::string readAll( const std::filesystem::path& path )
{
    std::ifstream input( path );
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

} // namespace

TEST_CASE( "platform log writes aligned lines to one file", "[platform][log]" )
{
    const auto file = tempLogFile( "platform_log_test.log" );

    platform::log::setLevel( platform::log::Level::Debug );
    platform::log::setConsoleEnabled( false );
    platform::log::setFile( file );

    platform::log::Record( platform::log::Level::Info, "fn", 42 )
        << "value " << 7 << " opt " << std::optional<int>{ 3 };

    platform::log::flush();

    const auto content = readAll( file );
    CHECK( content.find( " info [" ) != std::string::npos );
    CHECK( content.find( "[fn@42]" ) != std::string::npos );
    CHECK( content.find( "value 7 opt 3" ) != std::string::npos );

    platform::log::setFile( {} );
    std::error_code error;
    std::filesystem::remove( file, error );
}

TEST_CASE( "platform log respects the level threshold", "[platform][log]" )
{
    const auto file = tempLogFile( "platform_log_level.log" );

    platform::log::setFile( file );
    platform::log::setLevel( platform::log::Level::Warning );

    CHECK_FALSE( platform::log::enabled( platform::log::Level::Info ) );
    CHECK( platform::log::enabled( platform::log::Level::Error ) );

    platform::log::setFile( {} );
    std::error_code error;
    std::filesystem::remove( file, error );
}
