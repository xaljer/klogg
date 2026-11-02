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

#ifndef KLOGG_PERSISTENTINFO_H
#define KLOGG_PERSISTENTINFO_H

#include <QString>

#include "settings.h"

// Qt-side adapter over the platform store registry. The documents themselves
// are Qt-free (platform::userdata); this class only adds the one-time legacy
// import and the engine configuration attachment.
// Clients must implement Persistable

struct app_settings {
};
struct session_settings {
};

class PersistentInfo {
  public:
    // Composition-root initialization, called once before anything reads or
    // writes a store: selects the store location, imports legacy QSettings
    // data into the platform documents and attaches the engine configuration
    // to the app document. See persistentinfo.cpp for why the order matters.
    static void initialize();

    // Qt-free TOML stores shared by the UI, state and engine configuration.
    // Forwarded to platform::userdata, so every consumer in the process sees the
    // same documents.
    static platform::Settings& getStore( app_settings );
    static platform::Settings& getStore( session_settings );

    // Persist the documents. The app document is synced with the engine
    // configuration first so its values are always current.
    static void saveAppStore();
    static void saveSessionStore();

    // Get config directory (supports portable mode)
    static QString getConfigDir();

  private:
    // Link-time knob defined by each binary: true for the portable build and
    // for the test binaries, false for the regular desktop build
    static const bool ForcePortable;

    // One-time import of the legacy QSettings/INI stores and the transitional
    // klogg_state.toml document; defined in legacy_settings_migration.cpp, the
    // only file in this module that still needs QSettings.
    static void migrateLegacySettings( platform::Settings& appStore,
                                       platform::Settings& sessionStore );
};
#endif
