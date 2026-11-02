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

#include "userdata.h"

#include <memory>
#include <mutex>
#include <system_error>
#include <utility>

#include "paths.h"
#include "platform_log.h"
#include "settings.h"

namespace platform::userdata {
namespace {

// Interim names written by earlier builds; adopted once at initialize().
constexpr const char InterimConfigFile[] = "klogg.toml";
constexpr const char InterimSessionFile[] = "klogg_session.toml";

struct Registry {
    bool forcePortable = false;
    bool initialized = false;
    std::filesystem::path directory;
    std::unique_ptr<Settings> config;
    std::unique_ptr<Settings> session;
};

// One lazily built registry per process, so the documents reach every caller as
// the same object.
Registry& registry()
{
    static Registry instance;
    return instance;
}

std::mutex& registryMutex()
{
    static std::mutex mutex;
    return mutex;
}

void loadStore( Settings& store )
{
    if ( !store.load() ) {
        LOG_ERROR << "Failed to load " << store.path().string()
                  << " - keeping the existing file unchanged";
    }
}

void adoptInterimNames( const std::filesystem::path& dir )
{
    adoptLegacyName( dir, InterimConfigFile, fileName( Kind::Config ) );
    adoptLegacyName( dir, InterimSessionFile, fileName( Kind::Session ) );
}

void build( Registry& instance )
{
    auto directory = paths::configurationDir( instance.forcePortable );

    std::error_code error;
    std::filesystem::create_directories( directory, error );

    instance.directory = std::move( directory );
    adoptInterimNames( instance.directory );

    instance.config = std::make_unique<Settings>(
        instance.directory / fileName( Kind::Config ) );
    instance.session = std::make_unique<Settings>(
        instance.directory / fileName( Kind::Session ) );

    loadStore( *instance.config );
    loadStore( *instance.session );
}

} // namespace

const char* fileName( Kind kind )
{
    switch ( kind ) {
    case Kind::Config:
        return "config.toml";
    case Kind::Session:
        return "session.toml";
    }
    return "config.toml";
}

bool adoptLegacyName( const std::filesystem::path& directory, const std::string& from,
                      const std::string& to )
{
    std::error_code error;
    const auto fromPath = directory / from;
    const auto toPath = directory / to;
    if ( std::filesystem::exists( toPath, error ) ) {
        return false;
    }
    if ( !std::filesystem::exists( fromPath, error ) ) {
        return false;
    }
    std::error_code renameError;
    std::filesystem::rename( fromPath, toPath, renameError );
    if ( renameError ) {
        LOG_WARNING << "Failed to adopt " << fromPath.string() << ": " << renameError.message();
        return false;
    }
    LOG_INFO << "Adopted legacy document " << fromPath.string() << " -> " << toPath.string();
    return true;
}

void initialize( bool forcePortable )
{
    auto& instance = registry();
    std::lock_guard lock( registryMutex() );

    if ( instance.initialized ) {
        if ( instance.forcePortable != forcePortable ) {
            LOG_WARNING << "User-data location policy changed after the documents were created; "
                        << "keeping " << instance.directory.string();
        }
        return;
    }

    instance.forcePortable = forcePortable;
    instance.initialized = true;
    build( instance );
}

Settings& store( Kind kind )
{
    auto& instance = registry();
    std::lock_guard lock( registryMutex() );

    if ( !instance.initialized ) {
        instance.initialized = true;
        build( instance );
    }
    return kind == Kind::Config ? *instance.config : *instance.session;
}

const std::filesystem::path& directory()
{
    auto& instance = registry();
    std::lock_guard lock( registryMutex() );

    if ( !instance.initialized ) {
        instance.initialized = true;
        build( instance );
    }
    return instance.directory;
}

} // namespace platform::userdata
