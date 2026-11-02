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

#include "uiuserdata.h"

#include "log.h"
#include "userdata.h"

namespace klogg::ui {

PredefinedFiltersCollection& predefinedFilters()
{
    static PredefinedFiltersCollection collection = [] {
        PredefinedFiltersCollection loaded;
        loaded.retrieveFromStorage( platform::userdata::config() );
        return loaded;
    }();
    return collection;
}

void savePredefinedFilters()
{
    auto& settings = platform::userdata::config();
    predefinedFilters().saveToStorage( settings );
    if ( !settings.save() ) {
        LOG_WARNING << "Failed to save predefined filters to " << settings.path().string();
    }
}

NamedSavedSearches& namedSavedSearches()
{
    static NamedSavedSearches searches = [] {
        NamedSavedSearches loaded;
        loaded.retrieveFromStorage( platform::userdata::config() );
        return loaded;
    }();
    return searches;
}

void saveNamedSavedSearches()
{
    auto& settings = platform::userdata::config();
    namedSavedSearches().saveToStorage( settings );
    if ( !settings.save() ) {
        LOG_WARNING << "Failed to save named searches to " << settings.path().string();
    }
}

SavedSearches& searchHistory()
{
    static SavedSearches history = [] {
        SavedSearches loaded;
        loaded.retrieveFromStorage( platform::userdata::session() );
        return loaded;
    }();
    return history;
}

void saveSearchHistory()
{
    auto& settings = platform::userdata::session();
    searchHistory().saveToStorage( settings );
    if ( !settings.save() ) {
        LOG_WARNING << "Failed to save search history to " << settings.path().string();
    }
}

RecentFiles& recentFiles()
{
    static RecentFiles files = [] {
        RecentFiles loaded;
        loaded.retrieveFromStorage( platform::userdata::session() );
        return loaded;
    }();
    return files;
}

void saveRecentFiles()
{
    auto& settings = platform::userdata::session();
    recentFiles().saveToStorage( settings );
    if ( !settings.save() ) {
        LOG_WARNING << "Failed to save recent files to " << settings.path().string();
    }
}

} // namespace klogg::ui
