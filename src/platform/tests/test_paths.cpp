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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "paths.h"

namespace {

constexpr char PathListSeparator =
#if defined( _WIN32 )
    ';';
#else
    ':';
#endif

std::string toUtf8( const std::filesystem::path& path )
{
    // C++20: u8string() is char8_t-based
    const auto text = path.u8string();
    return { reinterpret_cast<const char*>( text.data() ), text.size() };
}

// Temporary PATH/PATHEXT override; restored on destruction
class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable( const char* name, const std::string& value )
        : name_( name )
    {
        const char* previous = std::getenv( name );
        if ( previous != nullptr ) {
            previous_ = previous;
            hadPrevious_ = true;
        }
        assign( value );
    }

    ScopedEnvironmentVariable( const ScopedEnvironmentVariable& ) = delete;
    ScopedEnvironmentVariable& operator=( const ScopedEnvironmentVariable& ) = delete;

    ~ScopedEnvironmentVariable() { assign( hadPrevious_ ? previous_ : std::string{} ); }

private:
    void assign( const std::string& value )
    {
#if defined( _WIN32 )
        _putenv_s( name_.c_str(), value.c_str() );
#else
        setenv( name_.c_str(), value.c_str(), 1 );
#endif
    }

    std::string name_;
    std::string previous_;
    bool hadPrevious_ = false;
};

#if defined( _WIN32 )
void makeExecutable( const std::filesystem::path& )
{
    // Windows has no executable bit to set
}
#else
void makeExecutable( const std::filesystem::path& path )
{
    std::error_code error;
    std::filesystem::permissions( path,
                                  std::filesystem::perms::owner_exec
                                      | std::filesystem::perms::group_exec
                                      | std::filesystem::perms::others_exec,
                                  std::filesystem::perm_options::add, error );
}
#endif

std::string pathList( const std::vector<std::filesystem::path>& directories )
{
    std::string list;
    for ( const auto& directory : directories ) {
        if ( !list.empty() ) {
            list += PathListSeparator;
        }
        list += toUtf8( directory );
    }
    return list;
}

} // namespace

TEST_CASE( "paths: executableDir is available", "[platform][paths]" )
{
    CHECK_FALSE( platform::paths::executableDir().empty() );
}

TEST_CASE( "paths: portable configuration detection", "[platform][paths]" )
{
    auto dir = std::filesystem::temp_directory_path() / "klogg_paths_test";
    std::error_code error;
    std::filesystem::remove_all( dir, error );
    std::filesystem::create_directories( dir );

    CHECK_FALSE( platform::paths::isPortableConfigurationPresent( dir ) );

    {
        std::ofstream( dir / "config.toml" ) << "[x]\n";
        CHECK( platform::paths::isPortableConfigurationPresent( dir ) );
        std::filesystem::remove( dir / "config.toml" );
    }

    {
        std::ofstream( dir / "klogg.toml" ) << "[x]\n";
        CHECK( platform::paths::isPortableConfigurationPresent( dir ) );
        std::filesystem::remove( dir / "klogg.toml" );
    }

    {
        std::ofstream( dir / "klogg.conf" ) << "[x]\n";
        CHECK( platform::paths::isPortableConfigurationPresent( dir ) );
    }

    std::filesystem::remove_all( dir, error );
}

TEST_CASE( "paths: forced portable mode uses the executable directory",
           "[platform][paths]" )
{
    CHECK( platform::paths::configurationDir( true ) == platform::paths::executableDir() );
    if ( platform::paths::isPortableConfigurationPresent( platform::paths::executableDir() ) ) {
        CHECK( platform::paths::configurationDir( false ) == platform::paths::executableDir() );
    }
    else {
        CHECK( platform::paths::configurationDir( false ).filename() == "klogg" );
    }
}

TEST_CASE( "paths: findExecutable searches the PATH in order", "[platform][paths]" )
{
    const auto root = std::filesystem::temp_directory_path() / "klogg_paths_exec";
    std::error_code error;
    std::filesystem::remove_all( root, error );
    std::filesystem::create_directories( root / "first" );
    std::filesystem::create_directories( root / "second" );
    std::filesystem::create_directories( root / "shadowed" );

    // Windows only finds a bare name through its executable suffix
#if defined( _WIN32 )
    const std::string name = "klogg_paths_probe";
    const std::string fileName = name + ".exe";
#else
    const std::string name = "klogg_paths_probe";
    const std::string fileName = name;
#endif

    const auto firstExecutable = root / "first" / fileName;
    const auto secondExecutable = root / "second" / fileName;
    std::ofstream( firstExecutable ) << "x";
    std::ofstream( secondExecutable ) << "x";
    makeExecutable( firstExecutable );
    makeExecutable( secondExecutable );
    // A directory carrying the name must never be taken for the executable
    std::filesystem::create_directory( root / "shadowed" / fileName );

    const ScopedEnvironmentVariable path(
        "PATH", pathList( { root / "shadowed", root / "first", root / "second" } ) );

    const auto found = platform::paths::findExecutable( name );
    CHECK_FALSE( found.empty() );
    CHECK( std::filesystem::equivalent( found, firstExecutable, error ) );

    // A name with a directory component is resolved directly, not through PATH
    CHECK( std::filesystem::equivalent( platform::paths::findExecutable(
                                            toUtf8( secondExecutable ) ),
                                        secondExecutable, error ) );

    CHECK( platform::paths::findExecutable( "klogg_no_such_executable" ).empty() );
    CHECK( platform::paths::findExecutable( "" ).empty() );
    CHECK( platform::paths::findExecutable( toUtf8( root / "missing" / fileName ) ).empty() );

    {
        // Only the second directory has the executable
        const ScopedEnvironmentVariable onlySecond( "PATH", pathList( { root / "second" } ) );
        const auto fromSecond = platform::paths::findExecutable( name );
        CHECK_FALSE( fromSecond.empty() );
        CHECK( std::filesystem::equivalent( fromSecond, secondExecutable, error ) );
    }

    std::filesystem::remove_all( root, error );
}

#if !defined( _WIN32 )
TEST_CASE( "paths: findExecutable needs the executable bit", "[platform][paths]" )
{
    const auto root = std::filesystem::temp_directory_path() / "klogg_paths_exec_bit";
    std::error_code error;
    std::filesystem::remove_all( root, error );
    std::filesystem::create_directories( root );

    const std::string name = "klogg_paths_bit_probe";
    const auto plainFile = root / name;
    std::ofstream( plainFile ) << "x";

    const ScopedEnvironmentVariable path( "PATH", pathList( { root } ) );

    // No executable bit yet: not a match
    CHECK( platform::paths::findExecutable( name ).empty() );

    makeExecutable( plainFile );
    CHECK( std::filesystem::equivalent( platform::paths::findExecutable( name ), plainFile,
                                        error ) );

    std::filesystem::remove_all( root, error );
}
#endif

#if defined( _WIN32 )
TEST_CASE( "paths: findExecutable probes the PATHEXT suffixes", "[platform][paths]" )
{
    const auto root = std::filesystem::temp_directory_path() / "klogg_paths_exec_pathext";
    std::error_code error;
    std::filesystem::remove_all( root, error );
    std::filesystem::create_directories( root );

    const auto customExecutable = root / "klogg_paths_pathext_probe.xyz";
    std::ofstream( customExecutable ) << "x";

    const ScopedEnvironmentVariable path( "PATH", pathList( { root } ) );
    const ScopedEnvironmentVariable pathext( "PATHEXT", ".XYZ" );

    const auto found = platform::paths::findExecutable( "klogg_paths_pathext_probe" );
    CHECK_FALSE( found.empty() );
    CHECK( std::filesystem::equivalent( found, customExecutable, error ) );

    // A name with a suffix is taken as complete
    CHECK( platform::paths::findExecutable( "klogg_paths_pathext_probe.xyz" ).filename()
           == customExecutable.filename() );
    CHECK( platform::paths::findExecutable( "klogg_paths_pathext_probe.nope" ).empty() );

    std::filesystem::remove_all( root, error );
}
#endif

TEST_CASE( "paths: normalizedPathKey canonicalizes and folds", "[platform][paths]" )
{
    const auto root = std::filesystem::temp_directory_path() / "klogg_paths_key";
    std::error_code error;
    std::filesystem::remove_all( root, error );
    std::filesystem::create_directories( root );

    const auto existing = root / "existing.log";
    std::ofstream( existing ) << "x";

    // "sub/.." is folded away both for an existing and for a missing file
    const auto existingKey = platform::paths::normalizedPathKey( toUtf8( existing ) );
    CHECK( existingKey
           == platform::paths::normalizedPathKey(
               toUtf8( root / "sub" / ".." / "existing.log" ) ) );
    CHECK( platform::paths::normalizedPathKey( toUtf8( root / "missing.log" ) )
           == platform::paths::normalizedPathKey(
               toUtf8( root / "sub" / ".." / "missing.log" ) ) );
    CHECK( existingKey
           != platform::paths::normalizedPathKey( toUtf8( root / "other.log" ) ) );

    // Windows compares paths case-insensitively, POSIX does not
    const auto upperCase = root / "EXISTING.LOG";
    {
        const auto upperKey = platform::paths::normalizedPathKey( toUtf8( upperCase ) );
#if defined( _WIN32 )
        CHECK( existingKey == upperKey );
#else
        CHECK( existingKey != upperKey );
#endif
    }

    // A UTF-8 name survives the round trip unchanged
    const std::string unicodeName = "klogg_\xC3\xBCnicode.log";
    const auto unicodePath = root / platform::paths::pathFromUtf8( unicodeName );
    std::ofstream( unicodePath ) << "x";
    const auto unicodeKey = platform::paths::normalizedPathKey( toUtf8( unicodePath ) );
    CHECK( unicodeKey.find( unicodeName ) != std::string::npos );

    std::filesystem::remove_all( root, error );
}
