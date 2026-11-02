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

#ifndef KLOGG_LOGTYPESSTORE_H
#define KLOGG_LOGTYPESSTORE_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "klogg/api_events.h"

namespace klogg::ui {

// File-level (de)serialization of the log type document (the historical
// log_visualization.json format). This is also the seam tests use. `loadFailed`
// (optional) is set when the file exists but cannot be read or parsed.
std::vector<klogg::api::LogTypeConfig> loadLogTypesFromFile( const std::filesystem::path& file,
                                                             bool* loadFailed = nullptr );
bool saveLogTypesToFile( const std::filesystem::path& file,
                         const std::vector<klogg::api::LogTypeConfig>& configs );

// UI-side owner of the log type / event rule configuration. The store keeps the
// authoring model in memory, persists it to <configDir>/log_visualization.json
// (the historical core format, no migration) and pushes the whole list to the
// core session, which keeps an in-memory copy for event extraction.
class LogTypesStore {
public:
    static LogTypesStore& instance();

    // In-memory configuration, loaded from disk on first use
    const std::vector<klogg::api::LogTypeConfig>& configs();

    // Replaces the whole configuration, persists it and pushes it to the core
    // session. A configuration loaded from a damaged file is never overwritten.
    void replaceConfigs( std::vector<klogg::api::LogTypeConfig> configs );

    // Matches a config for a file path by its glob pattern, falling back to the
    // default config; nullopt when nothing matches. Repeats are by first match.
    std::optional<klogg::api::LogTypeConfig> matchForFile( const std::string& filePath );

    // Startup path: loads the persisted configuration and pushes it to the core
    // session. Called once from the composition root, after the API session.
    void applyPersistedState();

private:
    LogTypesStore() = default;

    void ensureLoaded();
    void pushToSession();
    void persist() const;

    std::vector<klogg::api::LogTypeConfig> configs_;
    bool initialized_ = false;
    // Set when the file existed but could not be loaded: saving then refuses to
    // overwrite it, so a damaged user file is never silently replaced
    bool loadFailed_ = false;
};

} // namespace klogg::ui

#endif // KLOGG_LOGTYPESSTORE_H
