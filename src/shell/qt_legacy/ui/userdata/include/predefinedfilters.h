/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014, 2015 Nicolas Bonnefon
 * and other contributors
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

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#ifndef PREDEFINEDFILTERS_H_
#define PREDEFINEDFILTERS_H_

#include <string>

#include "containers.h"
#include "settings.h"

struct PredefinedFilter {
    std::string name;
    std::string pattern;
    bool useRegex;
};

// Represents collection of filters read from settings file.
class PredefinedFiltersCollection final {
  public:
    using Collection = klogg::vector<PredefinedFilter>;

    static const char* persistableName()
    {
        return "PredefinedFiltersCollection";
    }

    Collection getFilters() const;
    void setFilters( const Collection& filters );

    void retrieveFromStorage( platform::Settings& settings );
    void saveToStorage( platform::Settings& settings ) const;

  private:
    static constexpr int PredefinedFiltersCollection_VERSION = 2;

    Collection filters_;
};

#endif
