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

#include "paths.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>
#include <vector>

#if defined( _WIN32 )
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <whereami.h>

namespace platform::paths {
namespace {

std::string environmentValue( const char* name )
{
    const char* value = std::getenv( name );
    return value != nullptr ? std::string{ value } : std::string{};
}

std::filesystem::path environmentPath( const char* name )
{
    const auto value = environmentValue( name );
    return value.empty() ? std::filesystem::path{} : std::filesystem::path{ value };
}

#if defined( _WIN32 )
constexpr char PathListSeparator = ';';
#else
constexpr char PathListSeparator = ':';
#endif

std::vector<std::string> splitPathList( const std::string& list )
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while ( start <= list.size() ) {
        const auto separator = list.find( PathListSeparator, start );
        const auto end = separator == std::string::npos ? list.size() : separator;
        auto part = list.substr( start, end - start );
        // Windows PATH entries may be quoted when they contain spaces
        if ( part.size() >= 2 && part.front() == '"' && part.back() == '"' ) {
            part = part.substr( 1, part.size() - 2 );
        }
        if ( !part.empty() ) {
            parts.push_back( std::move( part ) );
        }
        if ( separator == std::string::npos ) {
            break;
        }
        start = separator + 1;
    }
    return parts;
}

bool isRunnableFile( const std::filesystem::path& candidate )
{
    std::error_code error;
    if ( !std::filesystem::is_regular_file( candidate, error ) ) {
        return false;
    }
#if defined( _WIN32 )
    return true;
#else
    return ::access( candidate.c_str(), X_OK ) == 0;
#endif
}

#if defined( _WIN32 )
std::string toLowerAscii( std::string text )
{
    std::transform( text.begin(), text.end(), text.begin(), []( unsigned char c ) {
        return static_cast<char>( std::tolower( c ) );
    } );
    return text;
}

std::vector<std::string> executableSuffixes()
{
    auto suffixes = splitPathList( environmentValue( "PATHEXT" ) );
    // QStandardPaths::findExecutable always probed the usual set as well, so a
    // stripped environment does not lose the common cases
    for ( const auto* fallback : { ".com", ".exe", ".bat", ".cmd" } ) {
        const auto known
            = std::any_of( suffixes.begin(), suffixes.end(), [ fallback ]( const auto& suffix ) {
                  return toLowerAscii( suffix ) == fallback;
              } );
        if ( !known ) {
            suffixes.emplace_back( fallback );
        }
    }

    // Each suffix in both cases: a volume can be case-sensitive
    std::vector<std::string> probed;
    probed.reserve( suffixes.size() * 2 );
    for ( const auto& suffix : suffixes ) {
        probed.push_back( suffix );
        auto lower = toLowerAscii( suffix );
        if ( lower != suffix ) {
            probed.push_back( std::move( lower ) );
        }
    }
    return probed;
}
#endif

std::string toUtf8( const std::filesystem::path& path )
{
    const auto text = path.u8string();
    return { reinterpret_cast<const char*>( text.data() ), text.size() };
}

#if defined( _WIN32 )
std::string foldCase( const std::filesystem::path& path )
{
    const auto& native = path.native();
    std::wstring folded( native.size(), L'\0' );
    const auto written = ::LCMapStringEx( LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, native.data(),
                                          static_cast<int>( native.size() ), folded.data(),
                                          static_cast<int>( folded.size() ), nullptr, nullptr,
                                          0 );
    if ( written <= 0 ) {
        return toUtf8( path );
    }
    folded.resize( static_cast<std::size_t>( written ) );
    return toUtf8( std::filesystem::path{ folded } );
}
#else
std::string foldCase( const std::filesystem::path& path )
{
    return toUtf8( path );
}
#endif

std::filesystem::path osConfigurationDir()
{
    // App-specific subdirectory, matching QStandardPaths::AppConfigLocation
    // (which appends org/app) and the historical QSettings IniFormat layout.
#if defined( _WIN32 )
    const auto localAppData = environmentPath( "LOCALAPPDATA" );
    if ( !localAppData.empty() ) {
        return localAppData / "klogg";
    }
#elif defined( __APPLE__ )
    const auto home = environmentPath( "HOME" );
    if ( !home.empty() ) {
        return home / "Library" / "Application Support" / "klogg";
    }
#else
    const auto xdgConfig = environmentPath( "XDG_CONFIG_HOME" );
    if ( !xdgConfig.empty() ) {
        return xdgConfig / "klogg";
    }
    const auto home = environmentPath( "HOME" );
    if ( !home.empty() ) {
        return home / ".config" / "klogg";
    }
#endif
    return std::filesystem::current_path();
}

} // namespace

std::filesystem::path executableDir()
{
    int dirnameLength = 0;
    const auto pathLength = wai_getExecutablePath( nullptr, 0, &dirnameLength );
    if ( pathLength <= 0 ) {
        return {};
    }

    std::vector<char> path( static_cast<std::size_t>( pathLength ), '\0' );
    wai_getExecutablePath( path.data(), pathLength, &dirnameLength );
    return std::filesystem::path( std::string( path.data(), static_cast<std::size_t>( dirnameLength ) ) );
}

bool isPortableConfigurationPresent( const std::filesystem::path& executableDir )
{
    std::error_code error;
    // New document name first; the legacy names keep an existing portable
    // install portable until the user-data container adopts them.
    return std::filesystem::exists( executableDir / "config.toml", error )
           || std::filesystem::exists( executableDir / "klogg.toml", error )
           || std::filesystem::exists( executableDir / "klogg.conf", error );
}

std::filesystem::path configurationDir( bool forcePortable )
{
    const auto executable = executableDir();
    if ( forcePortable || isPortableConfigurationPresent( executable ) ) {
        return executable;
    }
    return osConfigurationDir();
}

std::string toGenericString( const std::filesystem::path& path )
{
    return path.generic_string();
}

std::filesystem::path findExecutable( const std::string& name )
{
    if ( name.empty() ) {
        return {};
    }

    const auto candidate = pathFromUtf8( name );
    if ( candidate.has_parent_path() ) {
        return isRunnableFile( candidate ) ? candidate : std::filesystem::path{};
    }

#if defined( _WIN32 )
    // A dotted name is taken as complete: only a suffix-less name gets the
    // PATHEXT probed, so "python3.11" is never turned into "python3.11.exe"
    const auto suffixes = name.find( '.' ) == std::string::npos ? executableSuffixes()
                                                                : std::vector<std::string>{};
#endif

    for ( const auto& directory : splitPathList( environmentValue( "PATH" ) ) ) {
        const auto base = pathFromUtf8( directory );
        if ( isRunnableFile( base / candidate ) ) {
            return base / candidate;
        }
#if defined( _WIN32 )
        for ( const auto& suffix : suffixes ) {
            const auto withSuffix = base / pathFromUtf8( name + suffix );
            if ( isRunnableFile( withSuffix ) ) {
                return withSuffix;
            }
        }
#endif
    }
    return {};
}

std::string normalizedPathKey( const std::string& path )
{
    const auto given = pathFromUtf8( path );

    std::error_code error;
    auto normalized = std::filesystem::canonical( given, error );
    if ( error ) {
        // Missing file (a log that is not created yet): the absolute path is
        // all there is to compare
        error.clear();
        normalized = std::filesystem::absolute( given, error );
        if ( error ) {
            normalized = given;
        }
        normalized = normalized.lexically_normal();
    }

    return foldCase( normalized );
}

} // namespace platform::paths
