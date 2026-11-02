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

#include "platform_log.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
namespace platform::log {
namespace {

std::mutex& logMutex()
{
    static std::mutex mutex;
    return mutex;
}

Level& logLevel()
{
    static Level level = Level::Info;
    return level;
}

bool& consoleEnabled()
{
    static bool enabled = false;
    return enabled;
}

std::unique_ptr<std::ofstream>& logFile()
{
    static std::unique_ptr<std::ofstream> file;
    return file;
}

std::string& logFilePathStorage()
{
    static std::string path;
    return path;
}

const char* levelName( Level level )
{
    switch ( level ) {
    case Level::Debug:
        return "debug";
    case Level::Info:
        return "info";
    case Level::Warning:
        return "warning";
    case Level::Error:
        return "error";
    }
    return "info";
}

std::string timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t( now );
    const auto millis
        = std::chrono::duration_cast<std::chrono::milliseconds>( now.time_since_epoch() ) % 1000;

    std::tm localTime{};
#if defined( _WIN32 )
    localtime_s( &localTime, &seconds );
#else
    localtime_r( &seconds, &localTime );
#endif

    std::ostringstream out;
    out << std::put_time( &localTime, "%Y-%m-%dT%H:%M:%S" ) << '.' << std::setfill( '0' )
        << std::setw( 3 ) << millis.count();
    return out.str();
}

} // namespace

void setLevel( Level level )
{
    std::lock_guard<std::mutex> lock( logMutex() );
    logLevel() = level;
}

void setConsoleEnabled( bool enabled )
{
    std::lock_guard<std::mutex> lock( logMutex() );
    consoleEnabled() = enabled;
}

bool setFile( std::filesystem::path path )
{
    std::lock_guard<std::mutex> lock( logMutex() );

    if ( path.empty() ) {
        logFile().reset();
        logFilePathStorage().clear();
        return true;
    }

    std::error_code error;
    if ( !path.parent_path().empty() ) {
        std::filesystem::create_directories( path.parent_path(), error );
    }

    // Keep one previous log so an unbounded run cannot fill the disk.
    constexpr std::uintmax_t kMaxLogFileSize = 5 * 1024 * 1024;
    const auto size = std::filesystem::file_size( path, error );
    if ( !error && size > kMaxLogFileSize ) {
        std::error_code rotateError;
        const auto previous = path.string() + ".1";
        std::filesystem::remove( previous, rotateError );
        std::filesystem::rename( path, previous, rotateError );
    }

    auto file = std::make_unique<std::ofstream>( path, std::ios::app );
    if ( !file->good() ) {
        std::cerr << "klogg: failed to open log file " << path.string() << std::endl;
        return false;
    }
    logFile() = std::move( file );
    logFilePathStorage() = path.string();
    return true;
}

bool enabled( Level level )
{
    std::lock_guard<std::mutex> lock( logMutex() );
    if ( logFile() == nullptr && !consoleEnabled() ) {
        return false;
    }
    return static_cast<int>( level ) >= static_cast<int>( logLevel() );
}

std::string filePath()
{
    std::lock_guard<std::mutex> lock( logMutex() );
    return logFilePathStorage();
}

void write( Level level, const std::string& message )
{
    if ( !enabled( level ) ) {
        return;
    }
    Record( level, "", 0 ) << message;
}

void flush()
{
    std::lock_guard<std::mutex> lock( logMutex() );
    if ( logFile() ) {
        logFile()->flush();
    }
}

Record::Record( Level level, const char* function, int line )
    : level_( level )
    , function_( function )
    , line_( line )
{
}

Record::~Record()
{
    std::lock_guard<std::mutex> lock( logMutex() );
    if ( logFile() == nullptr && !consoleEnabled() ) {
        return;
    }

    std::ostringstream line;
    line << timestamp() << ' ' << levelName( level_ ) << " ["
         << std::hash<std::thread::id>{}( std::this_thread::get_id() ) << "] ["
         << ( function_ != nullptr ? function_ : "" ) << "@" << line_ << "] " << buffer_.str()
         << '\n';

    const auto text = line.str();
    if ( logFile() ) {
        *logFile() << text;
        logFile()->flush();
    }
    if ( consoleEnabled() ) {
        std::cout << text;
    }
}

} // namespace platform::log
