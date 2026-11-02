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

#ifndef KLOGG_PLATFORM_SETTINGS_H
#define KLOGG_PLATFORM_SETTINGS_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace platform {

// A Qt-free, TOML-backed settings document. It mirrors the QSettings subset the
// state collections and configuration code use: scalar values addressed by
// (optionally slash-separated) keys, nested groups, and arrays of tables.
//
// A single instance owns one file; callers load() once at startup and save()
// after mutations. Section namespaces are the caller's responsibility; this
// class is deliberately schema-agnostic.
//
// Thread safety: every method that touches the document is guarded by an
// internal mutex, so concurrent set*/get*/save from several threads is safe
// (QSettings provided the same guarantee). Group and array navigation
// (beginGroup/endGroup/beginWriteArray/setArrayIndex/endArray) is per-instance
// state and is not isolated per thread: concurrent writers must use
// slash-separated keys or own their own group scope. path() is immutable.
class Settings {
public:
    explicit Settings( std::filesystem::path path );
    ~Settings();

    const std::filesystem::path& path() const;

    // Missing file is success (defaults apply); a malformed existing file
    // returns false and leaves the document empty.
    bool load();
    bool save() const;

    // Groups. beginGroup/endGroup nest; keys are resolved relative to the
    // current group unless they start with '/'.
    void beginGroup( const std::string& name );
    void endGroup();

    bool contains( const std::string& key ) const;
    void remove( const std::string& key );

    // Immediate children of the current group: childGroups are nested tables,
    // childKeys are scalars/arrays.
    std::vector<std::string> childGroups() const;
    std::vector<std::string> childKeys() const;

    void setBool( const std::string& key, bool value );
    void setInt( const std::string& key, int value );
    void setInt64( const std::string& key, int64_t value );
    void setUInt( const std::string& key, unsigned value );
    void setString( const std::string& key, const std::string& value );
    void setStringList( const std::string& key, const std::vector<std::string>& values );
    void setIntList( const std::string& key, const std::vector<int>& values );

    bool getBool( const std::string& key, bool fallback ) const;
    int getInt( const std::string& key, int fallback ) const;
    int64_t getInt64( const std::string& key, int64_t fallback ) const;
    unsigned getUInt( const std::string& key, unsigned fallback ) const;
    std::string getString( const std::string& key, const std::string& fallback = {} ) const;
    std::vector<std::string> getStringList( const std::string& key ) const;
    std::vector<int> getIntList( const std::string& key ) const;

    // Copies a top-level group (table/array) from another document into this
    // one; used for one-time store migrations. Returns false when absent.
    bool copyGroupFrom( const std::string& group, const Settings& source );

    // Arrays of tables, matching QSettings' beginWriteArray/setArrayIndex
    // protocol. beginWriteArray replaces the array with an empty one;
    // beginReadArray returns the element count. While an array is active,
    // set*/get* address fields inside the selected element.
    void beginWriteArray( const std::string& name );
    int beginReadArray( const std::string& name );
    void setArrayIndex( int index );
    void endArray();

private:
    // Implementation lives in the .cpp; the toml value type is not exposed.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace platform

#endif // KLOGG_PLATFORM_SETTINGS_H
