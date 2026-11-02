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

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <share.h>
#include <stdio.h>
#else
#include <unistd.h>
#endif

#include "settings.h"

namespace {

std::filesystem::path tempFile( const std::string& name )
{
    auto path = std::filesystem::temp_directory_path() / name;
    std::error_code error;
    std::filesystem::remove( path, error );
    return path;
}

void writeFile( const std::filesystem::path& path, const std::string& content )
{
    std::ofstream output( path, std::ios::trunc );
    output << content;
}

std::string readFile( const std::filesystem::path& path )
{
    std::ifstream input( path );
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

// Holds an existing file so that it cannot be opened: the transient I/O
// failure case. Windows takes an exclusive handle, POSIX drops the permission
// bits. valid() is false where the platform cannot enforce it (POSIX as root),
// so callers skip the case instead of asserting a failure that cannot happen.
class UnreadableFileGuard {
public:
    explicit UnreadableFileGuard( const std::filesystem::path& path );
    ~UnreadableFileGuard();

    UnreadableFileGuard( const UnreadableFileGuard& ) = delete;
    UnreadableFileGuard& operator=( const UnreadableFileGuard& ) = delete;

    bool valid() const;

private:
#ifdef _WIN32
    FILE* handle_ = nullptr;
#else
    std::filesystem::path path_;
    std::filesystem::perms savedPermissions_ = std::filesystem::perms::unknown;
#endif
};

#ifdef _WIN32

UnreadableFileGuard::UnreadableFileGuard( const std::filesystem::path& path )
    : handle_( _fsopen( path.string().c_str(), "rb", _SH_DENYRW ) )
{
}

UnreadableFileGuard::~UnreadableFileGuard()
{
    if ( handle_ != nullptr ) {
        std::fclose( handle_ );
    }
}

bool UnreadableFileGuard::valid() const
{
    return handle_ != nullptr;
}

#else

UnreadableFileGuard::UnreadableFileGuard( const std::filesystem::path& path )
    : path_( path )
{
    std::error_code error;
    savedPermissions_ = std::filesystem::status( path, error ).permissions();
    if ( error ) {
        path_.clear();
        return;
    }
    std::filesystem::permissions( path, std::filesystem::perms::none, error );
    if ( error ) {
        path_.clear();
    }
}

UnreadableFileGuard::~UnreadableFileGuard()
{
    if ( !path_.empty() ) {
        std::error_code error;
        std::filesystem::permissions( path_, savedPermissions_, error );
    }
}

bool UnreadableFileGuard::valid() const
{
    return !path_.empty() && ::geteuid() != 0;
}

#endif

} // namespace

TEST_CASE( "Settings round-trips scalars, groups and arrays of tables",
           "[platform][settings]" )
{
    const auto file = tempFile( "platform_settings_roundtrip.toml" );

    {
        platform::Settings settings( file );
        settings.beginGroup( "PredefinedFiltersCollection" );
        settings.setInt( "version", 1 );
        settings.setBool( "flag", true );
        settings.setString( "name", "hello" );

        settings.beginWriteArray( "filters" );
        settings.setArrayIndex( 0 );
        settings.setString( "name", "f1" );
        settings.setString( "filter", "abc" );
        settings.setBool( "regex", true );
        settings.setArrayIndex( 1 );
        settings.setString( "name", "f2" );
        settings.setBool( "regex", false );
        settings.endArray();

        settings.endGroup();
        REQUIRE( settings.save() );
    }

    {
        platform::Settings settings( file );
        REQUIRE( settings.load() );

        REQUIRE( settings.contains( "PredefinedFiltersCollection/version" ) );

        settings.beginGroup( "PredefinedFiltersCollection" );
        CHECK( settings.getInt( "version", -1 ) == 1 );
        CHECK( settings.getBool( "flag", false ) == true );
        CHECK( settings.getString( "name", "" ) == "hello" );

        const auto count = settings.beginReadArray( "filters" );
        REQUIRE( count == 2 );
        settings.setArrayIndex( 0 );
        CHECK( settings.getString( "name" ) == "f1" );
        CHECK( settings.getString( "filter" ) == "abc" );
        CHECK( settings.getBool( "regex", false ) == true );
        settings.setArrayIndex( 1 );
        CHECK( settings.getString( "name" ) == "f2" );
        CHECK( settings.getBool( "regex", true ) == false );
        settings.endArray();

        settings.endGroup();
    }

    std::error_code error;
    std::filesystem::remove( file, error );
}

TEST_CASE( "Settings falls back for absent keys and wrong types", "[platform][settings]" )
{
    const auto file = tempFile( "platform_settings_fallback.toml" );
    writeFile( file, "[group]\nname = \"text\"\n" );

    platform::Settings settings( file );
    REQUIRE( settings.load() );

    settings.beginGroup( "group" );
    CHECK( settings.getInt( "missing", 42 ) == 42 );
    CHECK( settings.getString( "missing", "def" ) == "def" );
    CHECK( settings.getInt( "name", 7 ) == 7 ); // stored as string, wrong type
    settings.endGroup();

    std::error_code error;
    std::filesystem::remove( file, error );
}

TEST_CASE( "Settings removes keys", "[platform][settings]" )
{
    const auto file = tempFile( "platform_settings_remove.toml" );
    writeFile( file, "[group]\nname = \"text\"\nkeep = 1\n" );

    platform::Settings settings( file );
    REQUIRE( settings.load() );
    REQUIRE( settings.contains( "group/name" ) );

    settings.remove( "group/name" );
    CHECK_FALSE( settings.contains( "group/name" ) );
    CHECK( settings.getInt( "group/keep", 0 ) == 1 );
    REQUIRE( settings.save() );

    std::error_code error;
    std::filesystem::remove( file, error );
}

TEST_CASE( "Settings supports nested arrays, groups and string lists",
           "[platform][settings]" )
{
    const auto file = tempFile( "platform_settings_nested.toml" );

    {
        platform::Settings settings( file );
        settings.beginGroup( "HighlighterSetCollection" );
        settings.setInt( "version", 1 );
        settings.setStringList( "active_sets", { "a", "b" } );

        settings.beginWriteArray( "sets" );
        settings.setArrayIndex( 0 );
        settings.beginGroup( "HighlighterSet" );
        settings.setString( "name", "set0" );
        settings.beginWriteArray( "highlighters" );
        settings.setArrayIndex( 0 );
        settings.setString( "regexp", "x" );
        settings.setArrayIndex( 1 );
        settings.setString( "regexp", "y" );
        settings.endArray();
        settings.endGroup();
        settings.endArray();
        settings.endGroup();
        REQUIRE( settings.save() );
    }

    {
        platform::Settings settings( file );
        REQUIRE( settings.load() );
        settings.beginGroup( "HighlighterSetCollection" );
        CHECK( settings.getInt( "version", 0 ) == 1 );
        CHECK( settings.getStringList( "active_sets" )
               == std::vector<std::string>{ "a", "b" } );

        REQUIRE( settings.beginReadArray( "sets" ) == 1 );
        settings.setArrayIndex( 0 );
        settings.beginGroup( "HighlighterSet" );
        CHECK( settings.getString( "name" ) == "set0" );
        REQUIRE( settings.beginReadArray( "highlighters" ) == 2 );
        settings.setArrayIndex( 0 );
        CHECK( settings.getString( "regexp" ) == "x" );
        settings.setArrayIndex( 1 );
        CHECK( settings.getString( "regexp" ) == "y" );
        settings.endArray();
        settings.endGroup();
        settings.endArray();
        settings.endGroup();
    }

    std::error_code error;
    std::filesystem::remove( file, error );
}

TEST_CASE( "Settings tolerates a missing file and reports a malformed one",
           "[platform][settings]" )
{
    const auto missing = tempFile( "platform_settings_missing.toml" );
    {
        platform::Settings settings( missing );
        CHECK( settings.load() == true );
    }

    const auto malformed = tempFile( "platform_settings_malformed.toml" );
    writeFile( malformed, "not = valid = toml [[[\n" );
    {
        platform::Settings settings( malformed );
        CHECK( settings.load() == false );
    }

    std::error_code error;
    std::filesystem::remove( malformed, error );
}

TEST_CASE( "Settings never overwrites a malformed file and keeps a backup",
           "[platform][settings]" )
{
    const auto file = tempFile( "platform_settings_corrupt_guard.toml" );
    const std::string original = "not = valid = toml [[[\n";
    writeFile( file, original );

    platform::Settings settings( file );
    REQUIRE( settings.load() == false );

    // A backup is written for recovery.
    CHECK( std::filesystem::exists( std::filesystem::path( file.string() + ".corrupt" ) ) );

    // Mutating and saving must not clobber the corrupt original.
    settings.setBool( "a/b", true );
    CHECK( settings.save() == false );

    std::ifstream input( file );
    std::ostringstream content;
    content << input.rdbuf();
    CHECK( content.str() == original );

    std::error_code error;
    std::filesystem::remove( file, error );
    std::filesystem::remove( std::filesystem::path( file.string() + ".corrupt" ), error );
}

TEST_CASE( "Settings replaces a scalar intermediate instead of throwing",
           "[platform][settings]" )
{
    const auto file = tempFile( "platform_settings_scalar_intermediate.toml" );
    writeFile( file, "a = 1\n" );

    platform::Settings settings( file );
    REQUIRE( settings.load() );

    // Writing "a/b" when "a" is a scalar must not throw toml::bad_cast.
    CHECK_NOTHROW( settings.setString( "a/b", "value" ) );
    CHECK_NOTHROW( settings.setBool( "a/c/d", true ) );
    REQUIRE( settings.save() );

    platform::Settings reloaded( file );
    REQUIRE( reloaded.load() );
    CHECK( reloaded.getString( "a/b", "" ) == "value" );
    CHECK( reloaded.getBool( "a/c/d", false ) == true );

    std::error_code error;
    std::filesystem::remove( file, error );
}

TEST_CASE( "Settings copyGroupFrom copies a whole top-level group", "[platform][settings]" )
{
    const auto sourceFile = tempFile( "platform_settings_copy_src.toml" );
    const auto targetFile = tempFile( "platform_settings_copy_dst.toml" );

    {
        platform::Settings source( sourceFile );
        source.beginGroup( "Group" );
        source.setInt( "version", 1 );
        source.setStringList( "items", { "a", "b" } );
        source.endGroup();
        REQUIRE( source.save() );
    }

    platform::Settings target( targetFile );
    platform::Settings source( sourceFile );
    REQUIRE( source.load() );

    CHECK( target.copyGroupFrom( "Group", source ) == true );
    CHECK( target.copyGroupFrom( "Missing", source ) == false );
    target.beginGroup( "Group" );
    CHECK( target.getInt( "version", 0 ) == 1 );
    CHECK( target.getStringList( "items" ) == std::vector<std::string>{ "a", "b" } );
    target.endGroup();

    std::error_code error;
    std::filesystem::remove( sourceFile, error );
    std::filesystem::remove( targetFile, error );
}

TEST_CASE( "Settings survives concurrent writers and saves", "[platform][settings]" )
{
    const auto file = tempFile( "platform_settings_concurrent.toml" );

    platform::Settings settings( file );
    REQUIRE( settings.load() );

    // One document, two threads — the way the GUI thread and the session
    // executor thread use it. Absolute keys only: group navigation is
    // per-instance state, not per-thread.
    constexpr int kIterations = 100;
    const std::vector<std::string> workers = { "worker_a", "worker_b" };

    std::atomic<bool> consistent{ true };
    std::vector<std::thread> threads;
    threads.reserve( workers.size() );
    for ( const auto& worker : workers ) {
        threads.emplace_back( [ &settings, &consistent, worker ] {
            const auto valueKey = worker + "/value";
            const auto nameKey = worker + "/name";
            for ( int i = 0; i < kIterations; ++i ) {
                settings.setInt( valueKey, i );
                settings.setString( nameKey, worker );
                settings.setStringList( worker + "/items", { "x", "y" } );
                if ( settings.getInt( valueKey, -1 ) != i
                     || settings.getString( nameKey, "" ) != worker ) {
                    consistent = false;
                }
                settings.contains( nameKey );
                settings.childGroups();
                settings.childKeys();
                if ( !settings.save() ) {
                    consistent = false;
                }
            }
        } );
    }
    for ( auto& thread : threads ) {
        thread.join();
    }

    CHECK( consistent.load() );

    // The file must still be parseable and hold both writers' keys.
    platform::Settings reloaded( file );
    REQUIRE( reloaded.load() );
    CHECK( reloaded.getInt( "worker_a/value", -1 ) == kIterations - 1 );
    CHECK( reloaded.getInt( "worker_b/value", -1 ) == kIterations - 1 );
    CHECK( reloaded.getString( "worker_a/name", "" ) == "worker_a" );
    CHECK( reloaded.getString( "worker_b/name", "" ) == "worker_b" );
    CHECK( reloaded.getStringList( "worker_a/items" ) == std::vector<std::string>{ "x", "y" } );

    std::error_code error;
    std::filesystem::remove( file, error );
}

TEST_CASE( "Settings saves an empty document that was never loaded", "[platform][settings]" )
{
    const auto file = tempFile( "platform_settings_empty_save.toml" );

    {
        // Nothing loaded (the file does not exist) and nothing set: the
        // document must serialize instead of throwing, and save() must report
        // success.
        platform::Settings settings( file );
        CHECK_FALSE( std::filesystem::exists( file ) );
        CHECK_NOTHROW( settings.save() );
        CHECK( settings.save() );
    }

    REQUIRE( std::filesystem::exists( file ) );
    {
        platform::Settings reloaded( file );
        CHECK( reloaded.load() );
        CHECK( reloaded.childKeys().empty() );
        CHECK( reloaded.childGroups().empty() );
        CHECK( reloaded.getInt( "anything", 7 ) == 7 );
    }

    // Same for the startup path: load() finds no file, the app saves defaults.
    const auto missing = tempFile( "platform_settings_empty_save_missing.toml" );
    {
        platform::Settings settings( missing );
        REQUIRE( settings.load() );
        CHECK( settings.save() );
    }
    CHECK( std::filesystem::exists( missing ) );
    {
        platform::Settings reloaded( missing );
        CHECK( reloaded.load() );
    }

    std::error_code error;
    std::filesystem::remove( file, error );
    std::filesystem::remove( missing, error );
}

TEST_CASE( "Settings keeps the target file when a save cannot replace it",
           "[platform][settings]" )
{
    const auto file = tempFile( "platform_settings_locked_target.toml" );
    writeFile( file, "existing = 1\n" );

    platform::Settings settings( file );
    REQUIRE( settings.load() );
    settings.setInt( "new_value", 2 );

    {
        // Windows refuses to replace a target another handle keeps open (it is
        // opened without FILE_SHARE_DELETE), so the rename fails and the save
        // falls back to copying over the file. On POSIX the rename replaces it.
        std::ifstream reader( file );
        CHECK( settings.save() );
    }

    {
        platform::Settings reloaded( file );
        REQUIRE( reloaded.load() );
        CHECK( reloaded.getInt( "existing", 0 ) == 1 );
        CHECK( reloaded.getInt( "new_value", 0 ) == 2 );
    }

    // When the target can neither be replaced nor copied over, the original
    // document survives: it is never removed first.
    const auto tempPath = std::filesystem::path( file.string() + ".tmp" );
    {
        // A directory where the temporary file has to go blocks the write on
        // every platform. Revoking permissions does not: replacing the target
        // only needs write access to the directory on POSIX, and CI containers
        // may run privileged.
        std::error_code error;
        std::filesystem::remove_all( tempPath, error );
        REQUIRE( std::filesystem::create_directory( tempPath, error ) );
        REQUIRE_FALSE( error );

        CHECK_FALSE( settings.save() );
        CHECK( std::filesystem::exists( file ) );
        CHECK( std::filesystem::is_directory( tempPath ) );

        std::filesystem::remove_all( tempPath, error );
    }

    {
        platform::Settings reloaded( file );
        REQUIRE( reloaded.load() );
        CHECK( reloaded.getInt( "new_value", 0 ) == 2 );
    }

    std::error_code error;
    std::filesystem::remove( file, error );
}

TEST_CASE( "Settings tells a transient read failure apart from a corrupt document",
           "[platform][settings]" )
{
    const auto corruptFile = tempFile( "platform_settings_error_kind_corrupt.toml" );
    writeFile( corruptFile, "not = valid = toml [[[\n" );
    const auto corruptBackup = std::filesystem::path( corruptFile.string() + ".corrupt" );
    std::error_code error;
    std::filesystem::remove( corruptBackup, error );

    {
        // A file that cannot be parsed: backed up, reported, and never
        // overwritten from then on.
        platform::Settings settings( corruptFile );
        CHECK( settings.load() == false );
        CHECK( std::filesystem::exists( corruptBackup ) );
        settings.setBool( "a/b", true );
        CHECK( settings.save() == false );
    }

    const auto unreadableFile = tempFile( "platform_settings_error_kind_locked.toml" );
    const std::string original = "value = 1\n";
    writeFile( unreadableFile, original );
    const auto unreadableBackup = std::filesystem::path( unreadableFile.string() + ".corrupt" );

    // A file that exists but cannot be opened: a transient failure. It is not
    // corruption, so no backup is claimed and the document stays usable.
    platform::Settings unreadable( unreadableFile );
    {
        UnreadableFileGuard guard( unreadableFile );
        if ( guard.valid() ) {
            CHECK( unreadable.load() == false );
            CHECK_FALSE( std::filesystem::exists( unreadableBackup ) );
        }
    }

    // The file on disk was left alone, and a later save is still allowed.
    CHECK( readFile( unreadableFile ) == original );

    unreadable.setInt( "value", 2 );
    CHECK( unreadable.save() );

    {
        platform::Settings reloaded( unreadableFile );
        REQUIRE( reloaded.load() );
        CHECK( reloaded.getInt( "value", 0 ) == 2 );
    }

    std::filesystem::remove( corruptFile, error );
    std::filesystem::remove( corruptBackup, error );
    std::filesystem::remove( unreadableFile, error );
    std::filesystem::remove( unreadableBackup, error );
}
