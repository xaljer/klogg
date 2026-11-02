/*
 * Copyright (C) 2013, 2014 Nicolas Bonnefon and other contributors
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

#include "session.h"

#include "log.h"
#include "log_qt.h"

#include <algorithm>
#include <cassert>

#include <QDir>
#include <QFileInfo>

#include "klogg/api_session.h"

#include "apisession.h"
#include "loadingstatus.h"
#include "sessioninfo.h"
#include "viewinterface.h"

namespace {

// Matches the dedup key in the API session so view lookup finds the views
// that share an API file entry
QString normalizeFileName( const QString& fileName )
{
    const QFileInfo fileInfo{ fileName };
    const auto canonical = fileInfo.canonicalFilePath();
    auto normalized
        = canonical.isEmpty() ? QDir::cleanPath( fileInfo.absoluteFilePath() ) : canonical;
#if defined( Q_OS_WIN )
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

} // namespace

Session::Session()
{
    SessionInfo::getSynced();

    quickFindPattern_ = std::make_shared<QuickFindPattern>();

}

Session::~Session()
{
    // Views are expected to be closed (or at least destroyed) before their
    // session; drop the API references of any that were not
    for ( const auto& [ view, file ] : openFiles_ ) {
        Q_UNUSED( view );
        klogg::ui::apiSession().closeFile( file.fileId );
    }
}

ViewInterface* Session::getViewIfOpen( const QString& file_name ) const
{
    const auto normalizedName = normalizeFileName( file_name );
    auto result = std::find_if( openFiles_.begin(), openFiles_.end(),
                                [ & ]( const std::pair<const ViewInterface*, OpenFile>& o ) {
                                    return ( o.second.normalizedName == normalizedName );
                                } );

    if ( result != openFiles_.end() )
        return result->second.view;
    else
        return nullptr;
}

ViewInterface* Session::open( const QString& file_name,
                              const std::function<ViewInterface*()>& view_factory )
{
    return openAlways( file_name, view_factory, nullptr );
}

void Session::close( const ViewInterface* view )
{
    const auto it = openFiles_.find( view );
    if ( it == openFiles_.end() ) {
        return;
    }

    const auto fileId = it->second.fileId;
    openFiles_.erase( it );

    // Views no longer hold engine objects, so the API reference (one per
    // open view of a shared file) can be dropped right away
    klogg::ui::apiSession().closeFile( fileId );
}

bool Session::isShared( const ViewInterface* view ) const
{
    const auto it = openFiles_.find( view );
    if ( it == openFiles_.end() ) {
        return false;
    }

    return std::any_of( openFiles_.begin(), openFiles_.end(),
                        [ & ]( const std::pair<const ViewInterface*, OpenFile>& o ) {
                            return o.first != view && o.second.fileId == it->second.fileId;
                        } );
}

QString Session::getFilename( const ViewInterface* view ) const
{
    const OpenFile* file = findOpenFileFromView( view );

    assert( file );

    return file->fileName;
}

void Session::getFileInfo( const ViewInterface* view, uint64_t* fileSize, uint64_t* fileNbLine,
                           QDateTime* lastModified ) const
{
    const OpenFile* file = findOpenFileFromView( view );

    assert( file );

    // The API-side FileInfo is refreshed on every engine loadingFinished
    // (open, reload, follow-driven growth), so it is as fresh as the native
    // live reads it replaces
    const auto info = klogg::ui::apiSession().fileInfo( file->fileId );
    if ( !info ) {
        LOG_WARNING << "getFileInfo failed: " << info.error().message;
        *fileSize = 0;
        *fileNbLine = 0;
        *lastModified = QDateTime{};
        return;
    }

    *fileSize = static_cast<uint64_t>( info->size_bytes );
    *fileNbLine = info->total_lines.get();
    *lastModified = info->last_modified_ms > 0
                        ? QDateTime::fromMSecsSinceEpoch( info->last_modified_ms )
                        : QDateTime{};
}

ViewInterface* Session::openAlways( const QString& file_name,
                                    const std::function<ViewInterface*()>& view_factory,
                                    const QString& view_context )
{
    auto& apiSession = klogg::ui::apiSession();

    // Indexing progress and completion reach the view through the per-file
    // session event route (registered in setData), so no open callback is
    // needed here. Missing files open as empty views so they can be followed
    // into existence.
    auto opened
        = apiSession.openFileAsync( file_name.toStdString(), {}, {}, true /* allowMissing */ );
    if ( !opened ) {
        LOG_ERROR << "Failed to open " << file_name << ": " << opened.error().message;
        return nullptr;
    }

    const auto fileId = opened->id;

    ViewInterface* view = view_factory();
    view->setData( fileId );
    view->setQuickFindPattern( quickFindPattern_ );

    if ( !view_context.isEmpty() )
        view->setViewContext( view_context );

    const auto info = apiSession.fileInfo( fileId );
    if ( !info ) {
        // Indexing failed while the view was being set up; the API entry is
        // already gone
        delete view;
        return nullptr;
    }

    if ( info->indexed ) {
        // Indexing completed (or the file was already open in another view)
        // before the view registered its per-file event route in setData:
        // deliver the missed completion through the view interface
        view->replayIndexingFinished();
    }

    openFiles_.insert( { view, { fileId, file_name, normalizeFileName( file_name ), view } } );

    return view;
}

Session::OpenFile* Session::findOpenFileFromView( const ViewInterface* view )
{
    assert( view );

    OpenFile* file = &( openFiles_.at( view ) );

    // OpenfileMap::at might throw out_of_range but since a view MUST always
    // be attached to a file, we don't handle it!

    return file;
}

const Session::OpenFile* Session::findOpenFileFromView( const ViewInterface* view ) const
{
    assert( view );

    const OpenFile* file = &( openFiles_.at( view ) );

    // OpenfileMap::at might throw out_of_range but since a view MUST always
    // be attached to a file, we don't handle it!

    return file;
}

std::vector<WindowSession> Session::windowSessions()
{
    const auto& session = SessionInfo::getSynced();
    const auto& sessionWindows = session.windows();

    std::vector<WindowSession> windows;
    for ( auto i = 0; i < sessionWindows.size(); ++i ) {
        windows.emplace_back( shared_from_this(), sessionWindows.at( i ), i );
    }

    return windows;
}

void WindowSession::save(
    const std::vector<std::tuple<const ViewInterface*, uint64_t,
                                 std::shared_ptr<const ViewContextInterface>>>& view_list,
    const QByteArray& geometry )
{
    LOG_DEBUG << "Session::save";

    std::vector<SessionInfo::OpenFile> session_files;
    for ( const auto& view : view_list ) {
        const ViewInterface* view_object;
        uint64_t top_line;
        std::shared_ptr<const ViewContextInterface> view_context;

        std::tie( view_object, top_line, view_context ) = view;

        const Session::OpenFile* file = appSession_->findOpenFileFromView( view_object );
        assert( file );

        LOG_DEBUG << "Saving " << file->fileName.toLocal8Bit().data() << " in session.";
        session_files.emplace_back( file->fileName, top_line, view_context->toString() );
    }

    auto& session = SessionInfo::getSynced();
    session.setOpenFiles( windowId_, session_files );
    session.setGeometry( windowId_, geometry );
    session.save();
}

std::vector<std::pair<QString, ViewInterface*>>
WindowSession::restore( const std::function<ViewInterface*()>& view_factory,
                        int* current_file_index )
{
    const auto& session = SessionInfo::getSynced();

    std::vector<SessionInfo::OpenFile> session_files = session.openFiles( windowId_ );
    LOG_DEBUG << "Session returned " << session_files.size();
    std::vector<std::pair<QString, ViewInterface*>> result;

    for ( const auto& file : session_files ) {
        LOG_DEBUG << "Create view for " << file.fileName;
        ViewInterface* view
            = appSession_->openAlways( file.fileName, view_factory, file.viewContext );
        if ( !view ) {
            LOG_WARNING << "Could not restore file " << file.fileName;
            continue;
        }
        result.emplace_back( file.fileName, view );
        openedFiles_.emplace_back( file.fileName );
    }

    *current_file_index = klogg::isize( result ) - 1;

    return result;
}

WindowSession::WindowSession( std::shared_ptr<Session> appSession, const QString& id, size_t index )
    : appSession_{ std::move( appSession ) }
    , windowId_{ id }
    , windowIndex_{ index }
{
    LOG_INFO << "created session for " << id;
    auto sessionInfo = SessionInfo::getSynced();
    sessionInfo.add( id );
    sessionInfo.save();
}

void WindowSession::restoreGeometry( QByteArray* geometry ) const
{
    const auto& session = SessionInfo::getSynced();
    *geometry = session.geometry( windowId_ );
}

bool WindowSession::close()
{
    LOG_INFO << "close window session " << windowId_;

    if ( appSession_->exitRequested() ) {
        return true;
    }

    auto& session = SessionInfo::getSynced();
    auto isRemoved = session.remove( windowId_ );
    session.save();

    LOG_INFO << "session is removed " << isRemoved;

    return !isRemoved;
}