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

#include "settings.h"

#include <toml.hpp>

#include <cstdint>
#include <fstream>
#include <mutex>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include "platform_log.h"

namespace platform {
namespace {

// A resolved path component: either a table key or an array index.
struct PathElement {
    std::string key;
    std::optional<std::size_t> index;
};

std::vector<std::string> splitKey( const std::string& key )
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while ( start < key.size() ) {
        const auto slash = key.find( '/', start );
        const auto end = slash == std::string::npos ? key.size() : slash;
        if ( end > start ) {
            parts.push_back( key.substr( start, end - start ) );
        }
        if ( slash == std::string::npos ) {
            break;
        }
        start = slash + 1;
    }
    return parts;
}

toml::value* findNode( toml::value& root, const std::vector<PathElement>& path )
{
    toml::value* node = &root;
    for ( const auto& element : path ) {
        if ( element.index ) {
            if ( !node->is_array() ) {
                return nullptr;
            }
            auto& array = node->as_array();
            if ( *element.index >= array.size() ) {
                return nullptr;
            }
            node = &array[ *element.index ];
        }
        else {
            if ( !node->is_table() ) {
                return nullptr;
            }
            auto& table = node->as_table();
            const auto it = table.find( element.key );
            if ( it == table.end() ) {
                return nullptr;
            }
            node = &it->second;
        }
    }
    return node;
}

toml::value& ensureNode( toml::value& root, const std::vector<PathElement>& path )
{
    toml::value* node = &root;
    for ( const auto& element : path ) {
        if ( element.index ) {
            if ( !node->is_array() ) {
                *node = toml::value( toml::value::array_type{} );
            }
            auto& array = node->as_array();
            while ( array.size() <= *element.index ) {
                array.emplace_back( toml::value::table_type{} );
            }
            node = &array[ *element.index ];
        }
        else {
            // A malformed document (or a user hand-edit) may have a scalar
            // where a table is expected; replace it instead of throwing
            // toml::bad_cast so a bad value can never terminate the process.
            if ( !node->is_table() ) {
                *node = toml::value( toml::value::table_type{} );
            }
            node = &node->as_table()[ element.key ];
        }
    }
    return *node;
}

} // namespace

struct Settings::Impl {
    struct ArrayScope {
        std::size_t nameIndex;
        int index;
    };

    explicit Impl( std::filesystem::path filePath )
        : path( std::move( filePath ) )
        , root( toml::value::table_type{} )
    {
    }

    // Prefix for keys resolved relative to the current group, inserting the
    // selected index of every enclosing array right after its name component.
    std::vector<PathElement> effectivePrefix() const
    {
        std::vector<PathElement> result;
        for ( std::size_t position = 0; position < group.size(); ++position ) {
            result.push_back( PathElement{ group[ position ], std::nullopt } );
            for ( const auto& array : arrays ) {
                if ( array.nameIndex == position ) {
                    result.push_back( PathElement{ {},
                                                   static_cast<std::size_t>( array.index < 0
                                                                                ? 0
                                                                                : array.index ) } );
                }
            }
        }
        return result;
    }

    std::vector<PathElement> resolve( const std::vector<std::string>& keys ) const
    {
        auto result = effectivePrefix();
        for ( const auto& key : keys ) {
            result.push_back( PathElement{ key, std::nullopt } );
        }
        return result;
    }

    std::vector<PathElement> arrayNodePath( const std::string& name ) const
    {
        auto result = effectivePrefix();
        result.push_back( PathElement{ name, std::nullopt } );
        return result;
    }

    std::filesystem::path path;
    // Always a table (also when nothing was loaded yet): serializing a
    // never-loaded document then yields a valid empty file instead of throwing
    // toml::serialization_error.
    toml::value root;
    std::vector<std::string> group;
    std::vector<ArrayScope> arrays;
    // Set when an existing file could not be parsed: save() then refuses to
    // overwrite it so a corrupt file is never silently replaced by an empty
    // document (a backup copy is made at load time).
    bool loadFailed = false;
    // Guards the document and the navigation state above. The GUI thread
    // (Persistable/saveAppStore) and the API session worker (shortcut/log-type
    // saves) share one instance; QSettings had the same protection. Recursive
    // so a public method may call another one while holding the lock.
    std::recursive_mutex mutex;
};

Settings::Settings( std::filesystem::path path )
    : impl_( std::make_unique<Impl>( std::move( path ) ) )
{
}

Settings::~Settings() = default;

const std::filesystem::path& Settings::path() const
{
    return impl_->path;
}

bool Settings::load()
{
    const std::lock_guard lock( impl_->mutex );

    if ( impl_->path.empty() ) {
        return true;
    }

    std::error_code error;
    if ( !std::filesystem::exists( impl_->path, error ) || error ) {
        return true;
    }

    try {
        impl_->root = toml::parse( impl_->path.string() );
        impl_->loadFailed = false;
        return true;
    } catch ( const toml::file_io_error& e ) {
        // The file exists but could not be opened (locked by another process,
        // permission denied): a transient failure, not corruption. The document
        // stays usable and a later save is still allowed.
        LOG_WARNING << "Failed to read " << impl_->path.string() << ": " << e.what();
        return false;
    } catch ( const std::exception& e ) {
        // Keep the file recoverable: back it up and refuse to overwrite it.
        impl_->root = toml::value( toml::value::table_type{} );
        impl_->loadFailed = true;
        std::error_code copyError;
        const auto backupPath = impl_->path.string() + ".corrupt";
        std::filesystem::copy_file( impl_->path, backupPath,
                                    std::filesystem::copy_options::overwrite_existing, copyError );
        if ( copyError ) {
            LOG_WARNING << "Failed to back up " << impl_->path.string() << ": "
                        << copyError.message();
        }
        LOG_WARNING << "Failed to parse " << impl_->path.string() << ": " << e.what()
                    << " — keeping the file and refusing to overwrite it";
        return false;
    }
}

bool Settings::save() const
{
    const std::lock_guard lock( impl_->mutex );

    if ( impl_->path.empty() ) {
        return false;
    }
    // Never clobber a file we failed to parse.
    if ( impl_->loadFailed ) {
        return false;
    }

    // Write to a sibling temp file and rename over the target so a crash or
    // power loss cannot leave a half-written document.
    auto tempPath = impl_->path;
    tempPath += ".tmp";

    {
        std::ofstream output( tempPath, std::ios::trunc );
        if ( !output ) {
            LOG_WARNING << "Failed to open " << tempPath.string() << " for writing";
            return false;
        }
        try {
            output << impl_->root;
        } catch ( const std::exception& e ) {
            // Report the failure instead of letting a serializer error escape:
            // save() never throws.
            LOG_WARNING << "Failed to serialize " << impl_->path.string() << ": " << e.what();
            output.close();
            std::error_code removeError;
            std::filesystem::remove( tempPath, removeError );
            return false;
        }
        output.flush();
        if ( !output.good() ) {
            LOG_WARNING << "Failed to write " << tempPath.string();
            output.close();
            std::error_code removeError;
            std::filesystem::remove( tempPath, removeError );
            return false;
        }
    }

    std::error_code error;
    std::filesystem::rename( tempPath, impl_->path, error );
    if ( error ) {
        // Windows rename does not always replace an existing target (an open
        // handle blocks it). Copying over the target keeps the original file in
        // place until the copy succeeds, unlike removing it first.
        std::error_code copyError;
        std::filesystem::copy_file( tempPath, impl_->path,
                                    std::filesystem::copy_options::overwrite_existing, copyError );
        if ( copyError ) {
            LOG_WARNING << "Failed to save " << impl_->path.string() << ": " << copyError.message();
            std::error_code removeError;
            std::filesystem::remove( tempPath, removeError );
            return false;
        }
    }

    // Already moved away by a successful rename.
    std::error_code removeError;
    std::filesystem::remove( tempPath, removeError );
    return true;
}

void Settings::beginGroup( const std::string& name )
{
    const std::lock_guard lock( impl_->mutex );
    impl_->group.push_back( name );
}

void Settings::endGroup()
{
    const std::lock_guard lock( impl_->mutex );
    if ( !impl_->group.empty() ) {
        impl_->group.pop_back();
    }
}

bool Settings::contains( const std::string& key ) const
{
    const std::lock_guard lock( impl_->mutex );
    return findNode( impl_->root, impl_->resolve( splitKey( key ) ) ) != nullptr;
}

void Settings::remove( const std::string& key )
{
    const std::lock_guard lock( impl_->mutex );

    const auto path = impl_->resolve( splitKey( key ) );
    if ( path.empty() ) {
        return;
    }

    std::vector<PathElement> parentPath( path.begin(), path.end() - 1 );
    const auto& last = path.back();
    if ( last.index ) {
        return;
    }

    auto* parent = findNode( impl_->root, parentPath );
    if ( parent != nullptr && parent->is_table() ) {
        parent->as_table().erase( last.key );
    }
}

std::vector<std::string> Settings::childGroups() const
{
    const std::lock_guard lock( impl_->mutex );

    std::vector<std::string> groups;
    const auto* node = findNode( impl_->root, impl_->effectivePrefix() );
    if ( node == nullptr || !node->is_table() ) {
        return groups;
    }
    for ( const auto& [name, value] : node->as_table() ) {
        if ( value.is_table() ) {
            groups.push_back( name );
        }
    }
    return groups;
}

std::vector<std::string> Settings::childKeys() const
{
    const std::lock_guard lock( impl_->mutex );

    std::vector<std::string> keys;
    const auto* node = findNode( impl_->root, impl_->effectivePrefix() );
    if ( node == nullptr || !node->is_table() ) {
        return keys;
    }
    for ( const auto& [name, value] : node->as_table() ) {
        if ( !value.is_table() ) {
            keys.push_back( name );
        }
    }
    return keys;
}

void Settings::setBool( const std::string& key, bool value )
{
    const std::lock_guard lock( impl_->mutex );
    ensureNode( impl_->root, impl_->resolve( splitKey( key ) ) ) = value;
}

void Settings::setInt( const std::string& key, int value )
{
    const std::lock_guard lock( impl_->mutex );
    ensureNode( impl_->root, impl_->resolve( splitKey( key ) ) )
        = static_cast<std::int64_t>( value );
}

void Settings::setInt64( const std::string& key, int64_t value )
{
    const std::lock_guard lock( impl_->mutex );
    ensureNode( impl_->root, impl_->resolve( splitKey( key ) ) )
        = static_cast<std::int64_t>( value );
}

void Settings::setUInt( const std::string& key, unsigned value )
{
    const std::lock_guard lock( impl_->mutex );
    ensureNode( impl_->root, impl_->resolve( splitKey( key ) ) )
        = static_cast<std::int64_t>( value );
}

void Settings::setString( const std::string& key, const std::string& value )
{
    const std::lock_guard lock( impl_->mutex );
    ensureNode( impl_->root, impl_->resolve( splitKey( key ) ) ) = value;
}

void Settings::setStringList( const std::string& key, const std::vector<std::string>& values )
{
    const std::lock_guard lock( impl_->mutex );

    toml::value::array_type array;
    array.reserve( values.size() );
    for ( const auto& value : values ) {
        array.emplace_back( value );
    }
    ensureNode( impl_->root, impl_->resolve( splitKey( key ) ) )
        = toml::value( std::move( array ) );
}

bool Settings::getBool( const std::string& key, bool fallback ) const
{
    const std::lock_guard lock( impl_->mutex );

    const auto* node = findNode( impl_->root, impl_->resolve( splitKey( key ) ) );
    if ( node == nullptr ) {
        return fallback;
    }
    try {
        return toml::get<bool>( *node );
    } catch ( const std::exception& ) {
        return fallback;
    }
}

int Settings::getInt( const std::string& key, int fallback ) const
{
    const std::lock_guard lock( impl_->mutex );

    const auto* node = findNode( impl_->root, impl_->resolve( splitKey( key ) ) );
    if ( node == nullptr ) {
        return fallback;
    }
    try {
        return static_cast<int>( toml::get<std::int64_t>( *node ) );
    } catch ( const std::exception& ) {
        return fallback;
    }
}

int64_t Settings::getInt64( const std::string& key, int64_t fallback ) const
{
    const std::lock_guard lock( impl_->mutex );

    const auto* node = findNode( impl_->root, impl_->resolve( splitKey( key ) ) );
    if ( node == nullptr ) {
        return fallback;
    }
    try {
        return static_cast<int64_t>( toml::get<std::int64_t>( *node ) );
    } catch ( const std::exception& ) {
        return fallback;
    }
}

unsigned Settings::getUInt( const std::string& key, unsigned fallback ) const
{
    const std::lock_guard lock( impl_->mutex );

    const auto* node = findNode( impl_->root, impl_->resolve( splitKey( key ) ) );
    if ( node == nullptr ) {
        return fallback;
    }
    try {
        return static_cast<unsigned>( toml::get<std::int64_t>( *node ) );
    } catch ( const std::exception& ) {
        return fallback;
    }
}

std::string Settings::getString( const std::string& key, const std::string& fallback ) const
{
    const std::lock_guard lock( impl_->mutex );

    const auto* node = findNode( impl_->root, impl_->resolve( splitKey( key ) ) );
    if ( node == nullptr ) {
        return fallback;
    }
    try {
        return toml::get<std::string>( *node );
    } catch ( const std::exception& ) {
        return fallback;
    }
}

std::vector<std::string> Settings::getStringList( const std::string& key ) const
{
    const std::lock_guard lock( impl_->mutex );

    std::vector<std::string> result;
    const auto* node = findNode( impl_->root, impl_->resolve( splitKey( key ) ) );
    if ( node == nullptr || !node->is_array() ) {
        return result;
    }
    for ( const auto& element : node->as_array() ) {
        try {
            result.push_back( toml::get<std::string>( element ) );
        } catch ( const std::exception& ) {
            break;
        }
    }
    return result;
}

void Settings::setIntList( const std::string& key, const std::vector<int>& values )
{
    const std::lock_guard lock( impl_->mutex );

    toml::value::array_type array;
    array.reserve( values.size() );
    for ( const auto value : values ) {
        array.emplace_back( static_cast<std::int64_t>( value ) );
    }
    ensureNode( impl_->root, impl_->resolve( splitKey( key ) ) )
        = toml::value( std::move( array ) );
}

std::vector<int> Settings::getIntList( const std::string& key ) const
{
    const std::lock_guard lock( impl_->mutex );

    std::vector<int> result;
    const auto* node = findNode( impl_->root, impl_->resolve( splitKey( key ) ) );
    if ( node == nullptr || !node->is_array() ) {
        return result;
    }
    for ( const auto& element : node->as_array() ) {
        try {
            result.push_back( static_cast<int>( toml::get<std::int64_t>( element ) ) );
        } catch ( const std::exception& ) {
            break;
        }
    }
    return result;
}

bool Settings::copyGroupFrom( const std::string& group, const Settings& source )
{
    // Copy the subtree under the source lock only: locking both documents at
    // once could deadlock when two threads copy in opposite directions.
    toml::value copied;
    {
        const std::lock_guard sourceLock( source.impl_->mutex );
        const auto& sourceTable = source.impl_->root.as_table();
        const auto it = sourceTable.find( group );
        if ( it == sourceTable.end() ) {
            return false;
        }
        copied = it->second;
    }

    const std::lock_guard lock( impl_->mutex );
    impl_->root.as_table()[ group ] = std::move( copied );
    return true;
}

void Settings::beginWriteArray( const std::string& name )
{
    const std::lock_guard lock( impl_->mutex );

    ensureNode( impl_->root, impl_->arrayNodePath( name ) )
        = toml::value( toml::value::array_type{} );
    impl_->group.push_back( name );
    impl_->arrays.push_back( Impl::ArrayScope{ impl_->group.size() - 1, -1 } );
}

int Settings::beginReadArray( const std::string& name )
{
    const std::lock_guard lock( impl_->mutex );

    const auto nodePath = impl_->arrayNodePath( name );
    impl_->group.push_back( name );
    impl_->arrays.push_back( Impl::ArrayScope{ impl_->group.size() - 1, -1 } );

    const auto* node = findNode( impl_->root, nodePath );
    if ( node == nullptr || !node->is_array() ) {
        return 0;
    }
    return static_cast<int>( node->as_array().size() );
}

void Settings::setArrayIndex( int index )
{
    const std::lock_guard lock( impl_->mutex );
    if ( !impl_->arrays.empty() ) {
        impl_->arrays.back().index = index;
    }
}

void Settings::endArray()
{
    const std::lock_guard lock( impl_->mutex );

    if ( !impl_->group.empty() ) {
        impl_->group.pop_back();
    }
    if ( !impl_->arrays.empty() ) {
        impl_->arrays.pop_back();
    }
}

} // namespace platform
