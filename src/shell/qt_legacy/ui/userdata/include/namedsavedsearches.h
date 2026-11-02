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

#ifndef NAMEDSAVEDSEARCHES_H
#define NAMEDSAVEDSEARCHES_H

#include <string>

#include "containers.h"
#include "settings.h"

// A named, restorable search; different from the transient search history
// kept by SavedSearches
struct NamedSavedSearch {
    std::string name;
    std::string pattern;
    bool isRegex = true;
    bool caseSensitive = false;
};

// Collection of named saved searches. Not a Persistable singleton: callers
// pass the shared app settings document explicitly.
class NamedSavedSearches {
public:
    const klogg::vector<NamedSavedSearch>& searches() const;
    void setSearches( const klogg::vector<NamedSavedSearch>& searches );

    void saveToStorage( platform::Settings& settings ) const;
    void retrieveFromStorage( platform::Settings& settings );

private:
    static constexpr int NamedSavedSearches_VERSION = 1;

    klogg::vector<NamedSavedSearch> searches_;
};

#endif
