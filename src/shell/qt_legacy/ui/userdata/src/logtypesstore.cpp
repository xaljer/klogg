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

#include "logtypesstore.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#include "apisession.h"
#include "event_mapping.h"
#include "eventvisualization.h"
#include "klogg/api_session.h"
#include "log.h"
#include "pcre2regex.h"
#include "settings.h"
#include "userdata.h"

namespace klogg::ui {
namespace {

// The log type document sits next to the app store, so it follows the same
// configuration directory in every mode (portable or not)
std::filesystem::path logTypesConfigFile()
{
    const auto& store = platform::userdata::config();
    return store.path().parent_path() / "log_visualization.json";
}

std::vector<::LogTypeConfig> toNative( const std::vector<klogg::api::LogTypeConfig>& configs )
{
    std::vector<::LogTypeConfig> native;
    native.reserve( configs.size() );
    for ( const auto& config : configs ) {
        native.push_back( klogg::api::nativeLogTypeFromApi( config ) );
    }
    return native;
}

std::vector<klogg::api::LogTypeConfig> toApi( const std::vector<::LogTypeConfig>& configs )
{
    std::vector<klogg::api::LogTypeConfig> api;
    api.reserve( configs.size() );
    for ( const auto& config : configs ) {
        api.push_back( klogg::api::apiLogTypeFromNative( config ) );
    }
    return api;
}

} // namespace

std::vector<klogg::api::LogTypeConfig> loadLogTypesFromFile( const std::filesystem::path& file,
                                                             bool* loadFailed )
{
    if ( loadFailed != nullptr ) {
        *loadFailed = false;
    }

    std::error_code error;
    if ( !std::filesystem::exists( file, error ) ) {
        return {};
    }

    std::ifstream input( file, std::ios::binary );
    if ( !input.is_open() ) {
        if ( loadFailed != nullptr ) {
            *loadFailed = true;
        }
        return {};
    }
    const std::string content( ( std::istreambuf_iterator<char>( input ) ),
                               std::istreambuf_iterator<char>() );

    EventVisualizationManager manager;
    const auto result = manager.deserializeConfiguration( content );
    if ( !result.success ) {
        if ( loadFailed != nullptr ) {
            *loadFailed = true;
        }
        return {};
    }
    return toApi( manager.getLogTypes() );
}

bool saveLogTypesToFile( const std::filesystem::path& file,
                         const std::vector<klogg::api::LogTypeConfig>& configs )
{
    EventVisualizationManager manager;
    manager.setLogTypes( toNative( configs ) );
    const auto content = manager.serializeConfiguration();

    std::ofstream output( file, std::ios::binary | std::ios::trunc );
    if ( !output.is_open() ) {
        return false;
    }
    output << content << '\n';
    return output.good();
}

LogTypesStore& LogTypesStore::instance()
{
    static LogTypesStore store;
    return store;
}

const std::vector<klogg::api::LogTypeConfig>& LogTypesStore::configs()
{
    ensureLoaded();
    return configs_;
}

void LogTypesStore::ensureLoaded()
{
    if ( initialized_ ) {
        return;
    }
    initialized_ = true;

    configs_ = loadLogTypesFromFile( logTypesConfigFile(), &loadFailed_ );
    if ( loadFailed_ ) {
        LOG_WARNING << "Failed to load " << logTypesConfigFile().string()
                    << " — keeping the file and refusing to overwrite it";
    }
}

void LogTypesStore::replaceConfigs( std::vector<klogg::api::LogTypeConfig> configs )
{
    ensureLoaded();
    configs_ = std::move( configs );
    persist();
    pushToSession();
}

void LogTypesStore::pushToSession()
{
    const auto result = apiSession().replaceLogTypeConfigs( configs_ );
    if ( !result ) {
        LOG_ERROR << "LogTypesStore: failed to push log type configs: " << result.error().message;
    }
}

void LogTypesStore::persist() const
{
    if ( loadFailed_ ) {
        LOG_WARNING << "Not saving log types: " << logTypesConfigFile().string()
                    << " failed to load";
        return;
    }

    if ( !saveLogTypesToFile( logTypesConfigFile(), configs_ ) ) {
        LOG_ERROR << "Could not write configuration file: " << logTypesConfigFile().string();
    }
}

std::optional<klogg::api::LogTypeConfig> LogTypesStore::matchForFile( const std::string& filePath )
{
    ensureLoaded();
    if ( configs_.empty() ) {
        return std::nullopt;
    }

    std::error_code error;
    const std::string fileName = std::filesystem::path( filePath ).filename().string();
    const std::string absolutePath = std::filesystem::absolute( filePath, error ).string();

    for ( const auto& config : configs_ ) {
        if ( !config.enabled || config.globPattern.empty() ) {
            continue;
        }
        const auto globRegex = Pcre2Regex::fromWildcard( config.globPattern, false );
        if ( globRegex.match( fileName ).hasMatch()
             || globRegex.match( absolutePath ).hasMatch() ) {
            return config;
        }
    }

    for ( const auto& config : configs_ ) {
        if ( config.enabled && config.isDefault && !config.parsingRule.parsePattern.empty() ) {
            return config;
        }
    }
    return std::nullopt;
}

void LogTypesStore::applyPersistedState()
{
    ensureLoaded();
    pushToSession();
}

} // namespace klogg::ui
