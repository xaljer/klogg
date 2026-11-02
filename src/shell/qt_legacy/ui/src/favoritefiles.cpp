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

#include "favoritefiles.h"
#include "log.h"

namespace {
constexpr const int FavoriteFilesVersion = 1;
}

void FavoriteFiles::add( const QString& path )
{
    if ( std::any_of( files_.begin(), files_.end(), FullPathComparator( path ) ) ) {
        return;
    }

    auto newFile = DisplayFilePath( path );

    auto lowerBound = std::lower_bound( files_.begin(), files_.end(), newFile,
                                        []( const DisplayFilePath& lhs, const DisplayFilePath& rhs ) {
                                            return lhs.displayName() < rhs.displayName();
                                        } );

    files_.insert( lowerBound, newFile );
}

void FavoriteFiles::remove( const QString& path )
{
    auto existingFile = std::find_if( files_.begin(), files_.end(), FullPathComparator( path ) );
    files_.erase( existingFile );
}

std::vector<DisplayFilePath> FavoriteFiles::favorites() const
{
    return files_;
}

void FavoriteFiles::saveToStorage( platform::Settings& settings ) const
{
    LOG_DEBUG << "FavoriteFiles::saveToStorage";

    settings.beginGroup( "FavoriteFiles" );
    settings.setInt( "version", FavoriteFilesVersion );
    settings.remove( "favorites" );
    settings.beginWriteArray( "favorites" );
    for ( auto i = 0u; i < files_.size(); ++i ) {
        settings.setArrayIndex( static_cast<int>( i ) );
        settings.setString( "name", files_.at( i ).fullPath().toStdString() );
    }
    settings.endArray();
    settings.endGroup();
}

void FavoriteFiles::retrieveFromStorage( platform::Settings& settings )
{
    LOG_DEBUG << "FavoriteFiles::retrieveFromStorage";

    files_.clear();

    if ( settings.contains( "FavoriteFiles/version" ) ) {
        settings.beginGroup( "FavoriteFiles" );
        if ( settings.getInt( "version", 0 ) == FavoriteFilesVersion ) {
            int size = settings.beginReadArray( "favorites" );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( static_cast<int>( i ) );
                QString path = QString::fromStdString( settings.getString( "name" ) );
                files_.emplace_back( path );
            }
            settings.endArray();
        }
        else {
            LOG_ERROR << "Unknown version of favorite files, ignoring it...";
        }
        settings.endGroup();
    }
}
