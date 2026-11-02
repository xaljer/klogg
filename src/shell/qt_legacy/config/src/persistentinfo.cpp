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

// The Qt-side half of the persistent settings: the documents live in
// platform::userdata and are Qt-free, this file only adds the legacy QSettings
// import (legacy_settings_migration.cpp).

#include <mutex>

#include "log.h"
#include "userdata.h"

#include "persistentinfo.h"

void PersistentInfo::initialize()
{
    // The location policy has to be known when the documents are built, and the
    // legacy import has to run before any consumer reads state (an earlier read
    // would see the pre-import document). std::call_once keeps repeated
    // composition-root calls harmless.
    static std::once_flag initialized;
    std::call_once( initialized, [] {
        platform::userdata::initialize( ForcePortable );
        migrateLegacySettings( platform::userdata::config(), platform::userdata::session() );
    } );
}

platform::Settings& PersistentInfo::getStore( app_settings )
{
    return platform::userdata::config();
}

platform::Settings& PersistentInfo::getStore( session_settings )
{
    return platform::userdata::session();
}

void PersistentInfo::saveAppStore()
{
    auto& store = platform::userdata::config();
    if ( !store.save() ) {
        LOG_WARNING << "Failed to save " << store.path().string();
    }
}

void PersistentInfo::saveSessionStore()
{
    auto& store = platform::userdata::session();
    if ( !store.save() ) {
        LOG_WARNING << "Failed to save " << store.path().string();
    }
}

QString PersistentInfo::getConfigDir()
{
    return QString::fromStdString( platform::userdata::directory().string() );
}
