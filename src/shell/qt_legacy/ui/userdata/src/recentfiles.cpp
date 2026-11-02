/*
 * Copyright (C) 2011 Nicolas Bonnefon and other contributors
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

#include <algorithm>
#include <filesystem>

#include "containers.h"
#include "log.h"
#include "paths.h"
#include "recentfiles.h"
#include "storage_utils.h"

void RecentFiles::removeRecent( const std::string& text )
{
    // First prune non existent files
    recentFiles_.erase( std::remove_if( recentFiles_.begin(), recentFiles_.end(),
                                        []( const std::string& fileName ) {
                                            return !std::filesystem::exists(
                                                platform::paths::pathFromUtf8( fileName ) );
                                        } ),
                        recentFiles_.end() );

    recentFiles_.erase(
        std::remove( recentFiles_.begin(), recentFiles_.end(), text ), recentFiles_.end() );
}

void RecentFiles::removeAll()
{
    recentFiles_.clear();
    recentFiles_.reserve( MAX_RECENT_FILES );
}
void RecentFiles::addRecent( const std::string& text )
{

    // Remove any copy of the about to be added filename
    removeRecent( text );

    // Add at the front
    recentFiles_.insert( recentFiles_.begin(), text );

    // Trim the list if it's too long
    while ( recentFiles_.size() > MAX_RECENT_FILES )
        recentFiles_.pop_back();
}

int RecentFiles::getNumberItemsToShow() const
{
    return std::min( klogg::isize( recentFiles_ ), filesHistoryMaxItemsToShow_ );
}

int RecentFiles::filesHistoryMaxItems() const
{
    return filesHistoryMaxItemsToShow_;
}

void RecentFiles::setFilesHistoryMaxItems( const int recentMaxFilesToShow )
{
    if ( recentMaxFilesToShow > 0 ) {
        filesHistoryMaxItemsToShow_ = std::clamp( recentMaxFilesToShow, 0, MAX_RECENT_FILES );
    }
}

std::vector<std::string> RecentFiles::recentFiles() const
{
    return recentFiles_;
}

//
// Persistable virtual functions implementation
//

void RecentFiles::saveToStorage( platform::Settings& settings ) const
{
    LOG_DEBUG << "RecentFiles::saveToStorage";

    settings.beginGroup( "RecentFiles" );
    settings.setInt( "version", RECENTFILES_VERSION );
    settings.remove( "filesHistory" );
    settings.beginWriteArray( "filesHistory" );
    for ( std::size_t i = 0; i < recentFiles_.size(); ++i ) {
        settings.setArrayIndex( static_cast<int>( i ) );
        settings.setString( "name", recentFiles_[ i ] );
    }
    settings.endArray();
    settings.setInt( "maxMenuItems", filesHistoryMaxItemsToShow_ );
    settings.endGroup();
}

void RecentFiles::retrieveFromStorage( platform::Settings& settings )
{
    LOG_DEBUG << "RecentFiles::retrieveFromStorage";

    removeAll();

    if ( settings.contains( "RecentFiles/version" ) ) {
        settings.beginGroup( "RecentFiles" );
        if ( settings.getInt( "version", 0 ) == RECENTFILES_VERSION ) {
            int size = settings.beginReadArray( "filesHistory" );
            size = std::min( size, MAX_RECENT_FILES );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( i );
                recentFiles_.push_back( settings.getString( "name" ) );
            }
            settings.endArray();
        }
        else {
            LOG_ERROR << "Unknown version of recent files, ignoring it...";
        }
        setFilesHistoryMaxItems(
            settings.getInt( "maxMenuItems", DEFAULT_MAX_ITEMS_TO_SHOW ) );
        settings.endGroup();
    }
}
