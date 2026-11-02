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

#ifndef KLOGG_KLOGG_VERSION_H
#define KLOGG_KLOGG_VERSION_H

// Qt UI binary and the packages shipping it.
const char* kloggUiVersion();

// Zero-Qt server and the Electron installer.
const char* kloggAppVersion();

// Core API / HTTP-JSON protocol revision; never shown to users.
const char* kloggApiVersion();

const char* kloggBuildDate();
const char* kloggCommit();
const char* kloggGitVersion();

#endif // KLOGG_KLOGG_VERSION_H
