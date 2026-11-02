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

#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include <QGuiApplication>

#include "configuration.h"
#include "persistentinfo.h"

// Each binary defines this itself; there is no shared Catch2 main
const bool PersistentInfo::ForcePortable = true;

int main( int argc, char* argv[] )
{
    // Configuration::saveToStorage uses QFontInfo, which aborts without a
    // QGuiApplication; offscreen keeps the tests headless
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    QGuiApplication a( argc, argv );

    // Composition root: initializes the store location and the legacy import
    // before anything reads a store (LogData's constructor requires the
    // Configuration persistable initialized)
    PersistentInfo::initialize();
    Configuration::getSynced();

    return Catch::Session().run( argc, argv );
}
