/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
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

#include "logger.h"

#include "paths.h"
#include "platform_log.h"

#include <filesystem>

namespace logging {
namespace {

platform::log::Level toPlatformLevel( LogLevel level )
{
    switch ( level ) {
    case LogLevel::Debug:
        return platform::log::Level::Debug;
    case LogLevel::Warning:
        return platform::log::Level::Warning;
    case LogLevel::Error:
    case LogLevel::Fatal:
        return platform::log::Level::Error;
    case LogLevel::None:
    case LogLevel::Info:
    default:
        return platform::log::Level::Info;
    }
}

std::filesystem::path defaultLogDirectory()
{
    const auto executableDir = platform::paths::executableDir();
    const bool portable = platform::paths::isPortableConfigurationPresent( executableDir );
    return portable ? ( executableDir / "log" )
                    : ( std::filesystem::temp_directory_path() / "klogg" );
}

} // namespace

void enableLogging( bool isEnabled, LogLevel logLevel )
{
    platform::log::setLevel( toPlatformLevel( logLevel ) );
    platform::log::setConsoleEnabled( isEnabled );
}

void enableFileLogging( bool isEnabled, LogLevel logLevel, const std::string& logDirectory )
{
    platform::log::setLevel( toPlatformLevel( logLevel ) );

    if ( !isEnabled ) {
        platform::log::setFile( {} );
        return;
    }

    const auto directory = logDirectory.empty() ? defaultLogDirectory()
                                                : std::filesystem::path( logDirectory );
    platform::log::setFile( directory / "klogg.log" );
}
std::string logFilePath()
{
    return platform::log::filePath();
}

std::string logDirectory()
{
    return defaultLogDirectory().string();
}

} // namespace logging
