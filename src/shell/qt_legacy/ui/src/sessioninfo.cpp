/*
 * Copyright (C) 2011, 2014 Nicolas Bonnefon and other contributors
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

#include "sessioninfo.h"

#include "log.h"

constexpr int OPENFILES_VERSION = 1;
constexpr int SESSION_VERSION = 1;

void SessionInfo::retrieveFromStorage( platform::Settings& settings )
{
    LOG_DEBUG << "SessionInfo::retrieveFromStorage";

    settings.beginGroup( "Window" );

    if ( settings.getInt( "version", 0 ) == SESSION_VERSION ) {
        windows_.clear();
        const auto windowsCount = settings.beginReadArray( "windows" );
        for ( auto windowIndex = 0; windowIndex < windowsCount; ++windowIndex ) {
            settings.setArrayIndex( static_cast<int>( windowIndex ) );
            QString windowId = QString::fromStdString( settings.getString( "id" ) );
            auto window = Window{ windowId };
            window.geometry = QByteArray::fromBase64(
                QByteArray::fromStdString( settings.getString( "geometry" ) ) );

            if ( settings.contains( "OpenFiles/version" ) ) {
                settings.beginGroup( "OpenFiles" );
                if ( settings.getInt( "version", 0 ) == OPENFILES_VERSION ) {
                    int size = settings.beginReadArray( "openFiles" );
                    LOG_DEBUG << "SessionInfo: " << size << " files.";
                    for ( int i = 0; i < size; ++i ) {
                        settings.setArrayIndex( i );
                        QString file_name = QString::fromStdString( settings.getString( "fileName" ) );
                        uint64_t top_line
                            = static_cast<uint64_t>( settings.getInt64( "topLine", 0 ) );
                        QString view_context
                            = QString::fromStdString( settings.getString( "viewContext" ) );
                        QString recorderCommandName
                            = QString::fromStdString( settings.getString( "recorderCommandName" ) );
                        window.openFiles.emplace_back( file_name, top_line, view_context,
                                                       recorderCommandName );
                    }
                    settings.endArray();
                }
                else {
                    LOG_ERROR << "Unknown version of OpenFiles, ignoring it...";
                }
                settings.endGroup();
            }

            LOG_INFO << "Loaded settings for window session " << windowId;
            windows_.emplace_back( window );
        }
        settings.endArray();
    }
    else {
        LOG_ERROR << "Unknown version of session, ignoring it...";
    }

    settings.endGroup();
}

void SessionInfo::saveToStorage( platform::Settings& settings ) const
{
    LOG_DEBUG << "SessionInfo::saveToStorage";

    settings.beginGroup( "Window" );
    settings.setInt( "version", SESSION_VERSION );

    settings.remove( "windows" );
    settings.beginWriteArray( "windows" );
    for ( auto windowIndex = 0u; windowIndex < windows_.size(); ++windowIndex ) {
        const auto& window = windows_.at( windowIndex );
        settings.setArrayIndex( static_cast<int>( windowIndex ) );

        settings.setString( "id", window.id.toStdString() );
        settings.setString( "geometry", window.geometry.toBase64().toStdString() );

        settings.beginGroup( "OpenFiles" );
        settings.setInt( "version", OPENFILES_VERSION );
        settings.remove( "openFiles" );
        settings.beginWriteArray( "openFiles" );
        for ( unsigned i = 0; i < window.openFiles.size(); ++i ) {
            settings.setArrayIndex( static_cast<int>( i ) );
            const OpenFile* open_file = &( window.openFiles.at( i ) );
            settings.setString( "fileName", open_file->fileName.toStdString() );
            settings.setInt64( "topLine", static_cast<int64_t>( open_file->topLine ) );
            settings.setString( "viewContext", open_file->viewContext.toStdString() );
            settings.setString( "recorderCommandName", open_file->recorderCommandName.toStdString() );
        }
        settings.endArray();
        settings.endGroup(); // OpenFiles
    }
    settings.endArray();
    settings.endGroup(); // Window

    settings.beginWriteArray( "recorderBindings" );
    auto bindingIndex = 0;
    for ( auto it = recorderBindings_.begin(); it != recorderBindings_.end(); ++it, ++bindingIndex ) {
        settings.setArrayIndex( bindingIndex );
        settings.setString( "file", it.key().toStdString() );
        settings.setString( "command", it.value().toStdString() );
    }
    settings.endArray();
}
