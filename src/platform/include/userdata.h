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

#ifndef KLOGG_PLATFORM_USERDATA_H
#define KLOGG_PLATFORM_USERDATA_H

#include <filesystem>
#include <string>

namespace platform {

class Settings;

// Process-level user-data container. It is deliberately schema-agnostic: it
// only knows which documents exist, where they live and how to load them. The
// meaning of the keys, and how values are applied to the core API, belong to the
// shell (see docs/architecture/target-architecture.md).
namespace userdata {

// Storage categories. Each maps to one document in the configuration directory.
enum class Kind {
    Config,  // config.toml: persistent user configuration
    Session, // session.toml: transient per-run state (recent files, marks, ...)
};

// Document file name for a kind.
const char* fileName( Kind kind );

// Composition root: select the location policy and load every document once.
// Must run before any store() access; repeated calls are no-ops (a later policy
// change is ignored and logged). Legacy-name adoption runs here, once.
void initialize( bool forcePortable );

// The process-wide documents; every caller sees the same object.
Settings& store( Kind kind );
inline Settings& config()
{
    return store( Kind::Config );
}
inline Settings& session()
{
    return store( Kind::Session );
}

// Directory holding the documents and their sidecars.
const std::filesystem::path& directory();

// One-time rename of an interim document name. If `to` is absent and `from`
// exists in `directory`, renames from->to and returns true. Exposed for tests
// and used by initialize() for klogg.toml/klogg_session.toml.
bool adoptLegacyName( const std::filesystem::path& directory, const std::string& from,
                      const std::string& to );

} // namespace userdata
} // namespace platform

#endif // KLOGG_PLATFORM_USERDATA_H
