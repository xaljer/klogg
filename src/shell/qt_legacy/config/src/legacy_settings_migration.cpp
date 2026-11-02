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

// One-time import of the legacy QSettings/INI documents into the platform
// user-data documents. This is the only file that still depends on QSettings; it runs from
// PersistentInfo::initialize(), i.e. before any store is read.

#include <algorithm>
#include <filesystem>
#include <memory>
#include <system_error>

#include <QMetaType>
#include <QSettings>
#include <QStringList>
#include <QVariant>

#include "engine_options.h"
#include "log.h"
#include "paths.h"
#include "userdata.h"

#include "persistentinfo.h"

namespace {
constexpr const char LegacyStateFile[] = "klogg_state.toml";
constexpr const char MigrationMarker[] = "_meta/legacy_imported";

// State collections that once lived in a separate transitional document.
constexpr const char* LegacyStateGroups[] = { "HighlighterSetCollection",
                                              "PredefinedFiltersCollection",
                                              "NamedSavedSearches" };

bool isQSettingsArray( QSettings& settings )
{
    if ( !settings.contains( "size" ) ) {
        return false;
    }
    for ( const auto& group : settings.childGroups() ) {
        bool isNumeric = false;
        group.toInt( &isNumeric );
        if ( !isNumeric ) {
            return false;
        }
    }
    return true;
}

// Copies QSettings' "size + numeric child groups" arrays into TOML arrays.
void importGroupContents( QSettings& from, platform::Settings& to )
{
    for ( const auto& key : from.childKeys() ) {
        const QVariant value = from.value( key );
        auto name = key.toStdString();
        // Legacy dotted keys (e.g. "mainFont.family") map to nested tables.
        std::replace( name.begin(), name.end(), '.', '/' );

        switch ( value.userType() ) {
        case QMetaType::Bool:
            to.setBool( name, value.toBool() );
            break;
        case QMetaType::Int:
            to.setInt( name, value.toInt() );
            break;
        case QMetaType::UInt:
            to.setUInt( name, value.toUInt() );
            break;
        case QMetaType::LongLong:
            to.setInt64( name, value.toLongLong() );
            break;
        case QMetaType::ULongLong:
            to.setUInt( name, static_cast<unsigned>( value.toULongLong() ) );
            break;
        case QMetaType::QStringList: {
            const auto strings = value.toStringList();
            // splitterSizes is the one legacy int-list key; QSettings reads it
            // back as a string list, so convert it (others stay strings).
            if ( name == "defaultView/splitterSizes" ) {
                std::vector<int> ints;
                bool allNumeric = !strings.isEmpty();
                for ( const auto& entry : strings ) {
                    bool ok = false;
                    const auto number = entry.toInt( &ok );
                    if ( !ok ) {
                        allNumeric = false;
                        break;
                    }
                    ints.push_back( number );
                }
                if ( allNumeric ) {
                    to.setIntList( name, ints );
                    break;
                }
            }
            std::vector<std::string> out;
            for ( const auto& entry : strings ) {
                out.push_back( entry.toStdString() );
            }
            to.setStringList( name, out );
            break;
        }
        case QMetaType::QVariantList: {
            std::vector<int> ints;
            for ( const auto& entry : value.toList() ) {
                ints.push_back( entry.toInt() );
            }
            to.setIntList( name, ints );
            break;
        }
        default:
            to.setString( name, value.toString().toStdString() );
            break;
        }
    }

    for ( const auto& group : from.childGroups() ) {
        from.beginGroup( group );
        if ( isQSettingsArray( from ) ) {
            const int size = from.value( "size" ).toInt();
            to.beginWriteArray( group.toStdString() );
            for ( int i = 1; i <= size; ++i ) {
                from.beginGroup( QString::number( i ) );
                to.setArrayIndex( i - 1 );
                importGroupContents( from, to );
                from.endGroup();
            }
            to.endArray();
        }
        else {
            to.beginGroup( group.toStdString() );
            importGroupContents( from, to );
            to.endGroup();
        }
        from.endGroup();
    }
}

void importQSettings( QSettings& from, platform::Settings& to )
{
    if ( from.allKeys().isEmpty() ) {
        return;
    }
    importGroupContents( from, to );
}
} // namespace

void PersistentInfo::migrateLegacySettings( platform::Settings& appStore,
                                            platform::Settings& sessionStore )
{
    // One-time, marker-gated migration so it never runs twice and never
    // overwrites settings the user has since edited.
    if ( appStore.getBool( MigrationMarker, false ) ) {
        return;
    }

    const auto configDirectory = platform::userdata::directory();
    const bool portable = ( configDirectory == platform::paths::executableDir() );

    // QSettings is not copyable/movable; build each legacy reader explicitly.
    auto readLegacy = [ &configDirectory, portable ]( const char* appName ) {
        if ( portable ) {
            const auto fileName = std::string( appName ) == "klogg" ? "klogg.conf"
                                                                     : "klogg_session.conf";
            return std::make_unique<QSettings>(
                QString::fromStdString( ( configDirectory / fileName ).string() ),
                QSettings::IniFormat );
        }
        return std::make_unique<QSettings>(
#ifdef _WIN32
            QSettings::IniFormat,
#else
            QSettings::NativeFormat,
#endif
            QSettings::UserScope, QStringLiteral( "klogg" ), QString::fromUtf8( appName ) );
    };

    importQSettings( *readLegacy( "klogg" ), appStore );
    importQSettings( *readLegacy( "klogg_session" ), sessionStore );

    std::error_code error;

    // Merge the transitional state document (only once, and only for groups
    // still absent from the app document).
    const auto legacyStatePath = configDirectory / LegacyStateFile;
    if ( std::filesystem::exists( legacyStatePath, error ) ) {
        platform::Settings legacyStore( legacyStatePath );
        legacyStore.load();
        for ( const auto* group : LegacyStateGroups ) {
            if ( !appStore.contains( std::string( group ) + "/version" ) ) {
                appStore.copyGroupFrom( group, legacyStore );
            }
        }
    }

    // Legacy ANSI key: hideAnsiColorSequences -> ansiProcessing.
    if ( !appStore.contains( "view/ansiProcessing" )
         && appStore.contains( "view/hideAnsiColorSequences" ) ) {
        appStore.setInt( "view/ansiProcessing",
                         appStore.getBool( "view/hideAnsiColorSequences", false )
                             ? static_cast<int>( AnsiProcessing::StripOnly )
                             : static_cast<int>( AnsiProcessing::None ) );
    }

    appStore.setBool( MigrationMarker, true );
    sessionStore.setBool( MigrationMarker, true );
    if ( !appStore.save() ) {
        LOG_WARNING << "Failed to save " << appStore.path().string();
    }
    if ( !sessionStore.save() ) {
        LOG_WARNING << "Failed to save " << sessionStore.path().string();
    }

    // Keep the legacy files for rollback, but rename so they are not reused.
    if ( std::filesystem::exists( legacyStatePath, error ) ) {
        std::error_code renameError;
        std::filesystem::rename( legacyStatePath, legacyStatePath.string() + ".imported",
                                 renameError );
    }
}
