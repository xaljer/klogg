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

#ifndef KLOGG_UIUSERDATA_H
#define KLOGG_UIUSERDATA_H

#include "namedsavedsearches.h"
#include "predefinedfilters.h"
#include "recentfiles.h"
#include "savedsearches.h"

namespace klogg::ui {

// Process-wide UI user-data collections. Core no longer tracks filters, saved
// searches, search history or recent files: they live in the Qt layer now.
// Each instance is loaded once from the platform store the old core state layer
// bound it to (filters / named searches -> config, history / recent files ->
// session) and every mutation is persisted through the matching save helper.
PredefinedFiltersCollection& predefinedFilters();
void savePredefinedFilters();

NamedSavedSearches& namedSavedSearches();
void saveNamedSavedSearches();

SavedSearches& searchHistory();
void saveSearchHistory();

RecentFiles& recentFiles();
void saveRecentFiles();

} // namespace klogg::ui

#endif // KLOGG_UIUSERDATA_H
