/*
 * Copyright (C) 2009, 2010, 2011 Nicolas Bonnefon and other contributors
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

// This file implements class SavedSearch

#include <algorithm>

#include "log.h"
#include "savedsearches.h"
#include "storage_utils.h"

void SavedSearches::addRecent( const std::string& text )
{
    // We're not interested in blank lines
    if ( text.empty() )
        return;

    savedSearches_.erase(
        std::remove( savedSearches_.begin(), savedSearches_.end(), text ), savedSearches_.end() );
    savedSearches_.insert( savedSearches_.begin(), text );

    trim();
}

std::vector<std::string> SavedSearches::recentSearches() const
{
    return savedSearches_;
}

int SavedSearches::historySize() const
{
    return historySize_;
}

void SavedSearches::setHistorySize( int historySize )
{
    historySize_ = historySize;
    trim();
}

void SavedSearches::clear()
{
    savedSearches_.clear();
}

void SavedSearches::trim()
{
    while ( savedSearches_.size() > static_cast<size_t>( historySize_ ) )
        savedSearches_.pop_back();
}

//
// Persistable virtual functions implementation
//

void SavedSearches::saveToStorage( platform::Settings& settings ) const
{
    LOG_DEBUG << "SavedSearches::saveToStorage";

    settings.beginGroup( "SavedSearches" );
    settings.setInt( "version", SAVEDSEARCHES_VERSION );
    settings.remove( "searchHistory" );
    settings.beginWriteArray( "searchHistory" );
    for ( std::size_t i = 0; i < savedSearches_.size(); ++i ) {
        settings.setArrayIndex( static_cast<int>( i ) );
        settings.setString( "string", savedSearches_[ i ] );
    }
    settings.endArray();
    settings.setInt( "historySize", historySize_ );
    settings.endGroup();
}

void SavedSearches::retrieveFromStorage( platform::Settings& settings )
{
    LOG_DEBUG << "SavedSearches::retrieveFromStorage";

    savedSearches_.clear();

    if ( settings.contains( "SavedSearches/version" ) ) {
        settings.beginGroup( "SavedSearches" );
        if ( settings.getInt( "version", 0 ) == SAVEDSEARCHES_VERSION ) {
            int size = settings.beginReadArray( "searchHistory" );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( i );
                savedSearches_.push_back( settings.getString( "string" ) );
            }
            settings.endArray();
            historySize_ = settings.getInt( "historySize", MaxNumberOfRecentSearches );
        }
        else {
            LOG_ERROR << "Unknown version of saved searches, ignoring it...";
        }
        settings.endGroup();
    }
}
