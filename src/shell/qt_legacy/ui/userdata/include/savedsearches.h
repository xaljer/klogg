/*
 * Copyright (C) 2009, 2011 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SAVEDSEARCHES_H
#define SAVEDSEARCHES_H

#include <string>
#include <vector>

#include "settings.h"

// Keeps track of the previously used searches and allows the application
// to retrieve them.
class SavedSearches final {
  public:
    static const char* persistableName()
    {
        return "SavedSearches";
    }
    
    // Adds the passed search to the list of recently used searches
    void addRecent( const std::string& text );

    // Returns a list of recent searches (newer first)
    std::vector<std::string> recentSearches() const;

    int historySize() const;
    void setHistorySize(int historySize);

    void clear();

    // Reads/writes the current config in the platform settings document passed
    void saveToStorage( platform::Settings& settings ) const;
    void retrieveFromStorage( platform::Settings& settings );

  private:
    void trim();

  private:
    static constexpr int SAVEDSEARCHES_VERSION = 1;

    static constexpr int MaxNumberOfRecentSearches = 100;

    int historySize_ = MaxNumberOfRecentSearches;
    std::vector<std::string> savedSearches_;
};

#endif
