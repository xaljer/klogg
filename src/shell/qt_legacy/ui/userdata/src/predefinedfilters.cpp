/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
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
 * Copyright (C) 2019 Anton Filimonov and other contributors
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

#include "predefinedfilters.h"

#include "log.h"
#include "storage_utils.h"

void PredefinedFiltersCollection::retrieveFromStorage( platform::Settings& settings )
{
    LOG_DEBUG << "PredefinedFiltersCollection::retrieveFromStorage";

    if ( settings.contains( "PredefinedFiltersCollection/version" ) ) {
        settings.beginGroup( "PredefinedFiltersCollection" );
        if ( settings.getInt( "version", 0 ) <= PredefinedFiltersCollection_VERSION ) {
            filters_.clear();

            int size = settings.beginReadArray( "filters" );

            filters_.reserve( static_cast<std::size_t>( size ) );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( i );

                filters_.push_back( { settings.getString( "name" ),
                                      settings.getString( "filter" ),
                                      settings.getBool( "regex", true ) } );
            }
            settings.endArray();
        }
        else {
            LOG_ERROR << "Unknown version of PredefinedFiltersCollection, ignoring it...";
        }
        settings.endGroup();
    }
}

void PredefinedFiltersCollection::saveToStorage( platform::Settings& settings ) const
{
    LOG_DEBUG << "PredefinedFiltersCollection::saveToStorage";

    settings.beginGroup( "PredefinedFiltersCollection" );
    settings.setInt( "version", PredefinedFiltersCollection_VERSION );

    settings.remove( "filters" );

    settings.beginWriteArray( "filters" );
    int arrayIndex = 0;
    for ( const auto& filter : filters_ ) {
        settings.setArrayIndex( arrayIndex );
        settings.setString( "name", filter.name );
        settings.setString( "filter", filter.pattern );
        settings.setBool( "regex", filter.useRegex );

        arrayIndex++;
    }
    settings.endArray();
    settings.endGroup();
}

PredefinedFiltersCollection::Collection PredefinedFiltersCollection::getFilters() const
{
    return filters_;
}

void PredefinedFiltersCollection::setFilters( const Collection& filters )
{
    filters_ = filters;
}
