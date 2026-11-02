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

#ifndef KLOGG_PLATFORM_PATHS_H
#define KLOGG_PLATFORM_PATHS_H

#include <filesystem>
#include <string>
#include <string_view>

namespace platform::paths {

// std::filesystem::u8path is deprecated in C++20 and removed in C++26, but every
// path crossing the API boundary is a UTF-8 std::string, so the conversion is
// still needed: path( std::string ) would decode the bytes as the native narrow
// encoding (ANSI code page on Windows) instead of UTF-8.
inline std::filesystem::path pathFromUtf8( const std::string& utf8 )
{
#if defined( __cpp_lib_char8_t )
    const auto* bytes = reinterpret_cast<const char8_t*>( utf8.data() );
    return std::filesystem::path( std::u8string_view( bytes, utf8.size() ) );
#else
    // Only reachable with a library predating char8_t, where u8path is neither
    // deprecated nor removable.
    return std::filesystem::u8path( utf8 );
#endif
}

// Directory containing the running executable.
std::filesystem::path executableDir();

// True when a configuration file (config.toml, or a legacy klogg.toml /
// klogg.conf) sits next to the executable, which selects portable mode.
bool isPortableConfigurationPresent( const std::filesystem::path& executableDir );

// Directory that holds config.toml / session.toml. Portable mode uses the
// executable directory; otherwise the platform's per-user config location.
std::filesystem::path configurationDir( bool forcePortable );

// Path string with '/' separators, matching how the old code compared paths.
std::string toGenericString( const std::filesystem::path& path );

// Looks `name` up in the PATH environment variable and returns the first match,
// or an empty path when there is none. A name carrying a directory component is
// resolved directly instead. On Windows a name without a suffix is also probed
// with the PATHEXT suffixes (.EXE first among the usual ones), which is what
// QStandardPaths::findExecutable used to do for bare names.
// `name` is UTF-8, like every path crossing the API boundary.
std::filesystem::path findExecutable( const std::string& name );

// Comparison key for one file path (UTF-8 in, UTF-8 out): the canonical path
// when it exists - symlinks and "."/".." resolved - and the absolute lexically
// normalized path otherwise. Windows folds the key to lower case because its
// filesystems compare paths case-insensitively.
std::string normalizedPathKey( const std::string& path );

} // namespace platform::paths

#endif // KLOGG_PLATFORM_PATHS_H
