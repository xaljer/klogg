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

#include "namedsavedsearches.h"

#include "log.h"
#include "storage_utils.h"

const klogg::vector<NamedSavedSearch>& NamedSavedSearches::searches() const
{
    return searches_;
}

void NamedSavedSearches::setSearches( const klogg::vector<NamedSavedSearch>& searches )
{
    searches_ = searches;
}

void NamedSavedSearches::saveToStorage( platform::Settings& settings ) const
{
    LOG_DEBUG << "NamedSavedSearches::saveToStorage";

    settings.beginGroup( "NamedSavedSearches" );
    settings.setInt( "version", NamedSavedSearches_VERSION );

    settings.remove( "searches" );

    settings.beginWriteArray( "searches" );
    int arrayIndex = 0;
    for ( const auto& search : searches_ ) {
        settings.setArrayIndex( arrayIndex );
        settings.setString( "name", search.name );
        settings.setString( "pattern", search.pattern );
        settings.setBool( "regex", search.isRegex );
        settings.setBool( "case_sensitive", search.caseSensitive );

        arrayIndex++;
    }
    settings.endArray();
    settings.endGroup();
}

void NamedSavedSearches::retrieveFromStorage( platform::Settings& settings )
{
    LOG_DEBUG << "NamedSavedSearches::retrieveFromStorage";

    searches_.clear();

    if ( settings.contains( "NamedSavedSearches/version" ) ) {
        settings.beginGroup( "NamedSavedSearches" );
        if ( settings.getInt( "version", 0 ) <= NamedSavedSearches_VERSION ) {
            int size = settings.beginReadArray( "searches" );

            searches_.reserve( static_cast<std::size_t>( size ) );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( i );

                searches_.push_back( { settings.getString( "name" ),
                                       settings.getString( "pattern" ),
                                       settings.getBool( "regex", true ),
                                       settings.getBool( "case_sensitive", false ) } );
            }
            settings.endArray();
        }
        else {
            LOG_ERROR << "Unknown version of NamedSavedSearches, ignoring it...";
        }
        settings.endGroup();
    }
}
