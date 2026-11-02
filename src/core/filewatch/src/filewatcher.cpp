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

#include "filewatcher.h"

#include "executor.h"
#include "log.h"
#include "paths.h"
#include "synchronization.h"
#include "throttler.h"

#include <efsw/efsw.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr auto kThrottleWindow = std::chrono::milliseconds( 250 );

struct WatchedFile {
    std::string name;
    std::filesystem::file_time_type mTime;
    std::uintmax_t size;

    bool operator==( const std::string& filename ) const
    {
        return name == filename;
    }

    bool operator!=( const WatchedFile& other ) const
    {
        return name != other.name || mTime != other.mTime || size != other.size;
    }
};

struct WatchedDirecotry {
    efsw::WatchID watchId;

    // filenames are in utf8
    std::string name;
    std::vector<WatchedFile> files;
};

bool isOnlyForPolling( const WatchedDirecotry& wd )
{
    return wd.watchId < 0;
}

// Strips a trailing path separator; efsw reports the watched directory with a
// trailing separator, while addFile stores it without one.
std::string stripTrailingSeparator( const std::string& directory )
{
    auto result = directory;
    while ( !result.empty() && ( result.back() == '/' || result.back() == '\\' ) ) {
        result.pop_back();
    }
    return result;
}

// Builds the full UTF-8 path of a watched file, matching the form used by the
// registration call (same source path, same separators). Qt-built paths use '/'
// on every platform, while path::string() uses the native separator ('\' on
// Windows); convert to the generic form so both sides compare exactly.
std::string buildWatchPath( const std::string& directory, const std::string& filename )
{
    return ( platform::paths::pathFromUtf8( directory )
             / platform::paths::pathFromUtf8( filename ) )
        .generic_string();
}

} // namespace

// The efsw machinery. Only the coordinator thread calls into it (addFile,
// removeFile, enableWatch, checkWatches, notifyOnFileAction); handleFileAction
// runs on the efsw thread and only posts to the coordinator, so the watcher
// state never crosses threads other than through the queue.
class EfswFileWatcher final : public efsw::FileWatchListener {
  public:
    explicit EfswFileWatcher( klogg::SessionExecutor* coordinator )
        : coordinator_{ coordinator }
    {
    }

    void enableWatch( bool enable )
    {
        ScopedRecursiveLock lock( mutex_ );

        if ( nativeWatchEnabled_ == enable ) {
            return;
        }

        nativeWatchEnabled_ = enable;

        if ( enable ) {
            for ( auto& dir : watchedPaths_ ) {
                LOG_INFO << "Will reenable watch for " << dir.name;
                dir.watchId = watcher_.addWatch( dir.name, this, false );
            }
        }
        else {
            for ( const auto& dir : watchedPaths_ ) {
                LOG_INFO << "Will disable watch for " << dir.name;
                watcher_.removeWatch( dir.watchId );
            }
        }
    }

    void addFile( const std::string& fullFileName )
    {
        ScopedRecursiveLock lock( mutex_ );

        LOG_DEBUG << fullFileName;

        const std::filesystem::path filePath = platform::paths::pathFromUtf8( fullFileName );
        const auto directory = filePath.parent_path().generic_string();
        const auto filename = filePath.filename().string();

        std::error_code errorCode;
        auto watchedFile
            = WatchedFile{ filename,
                           std::filesystem::last_write_time( filePath, errorCode ),
                           errorCode ? 0 : std::filesystem::file_size( filePath, errorCode ) };

        const auto wasEmpty = watchedPaths_.empty();

        auto watchedDirectory
            = std::find_if( watchedPaths_.begin(), watchedPaths_.end(),
                            [ &directory ]( const auto& wd ) { return wd.name == directory; } );

        const auto tryWatchDirectory = [ this ]( const std::string& path ) {
            auto watchId = nativeWatchEnabled_ ? watcher_.addWatch( path, this, false ) : -111;

            if ( watchId < 0 ) {
                LOG_WARNING << "failed to add watch " << path << " error " << watchId;
            }

            return watchId;
        };

        if ( watchedDirectory == watchedPaths_.end() ) {
            watchedPaths_.push_back(
                { tryWatchDirectory( directory ), directory, { std::move( watchedFile ) } } );
        }
        else {
            if ( isOnlyForPolling( *watchedDirectory ) ) {
                watchedDirectory->watchId = tryWatchDirectory( directory );
            }

            if ( std::find( watchedDirectory->files.begin(), watchedDirectory->files.end(),
                            watchedFile.name )
                 != watchedDirectory->files.end() ) {
                LOG_DEBUG << "already watching " << watchedFile.name << " in " << directory;
                return;
            }

            watchedDirectory->files.emplace_back( std::move( watchedFile ) );
        }

        if ( wasEmpty ) {
            watcher_.watch();
        }
    }

    void removeFile( const std::string& fullFileName )
    {
        ScopedRecursiveLock lock( mutex_ );

        LOG_DEBUG << fullFileName;

        const auto directory
            = platform::paths::pathFromUtf8( fullFileName ).parent_path().generic_string();

        auto watchedDirectory
            = std::find_if( watchedPaths_.begin(), watchedPaths_.end(),
                            [ &directory ]( const auto& wd ) { return wd.name == directory; } );

        if ( watchedDirectory != watchedPaths_.end() ) {
            const auto filename = platform::paths::pathFromUtf8( fullFileName ).filename().string();

            auto& files = watchedDirectory->files;

            auto watchedFile = std::find( files.begin(), files.end(), filename );

            if ( watchedFile != files.end() ) {
                files.erase( watchedFile );
            }

            if ( files.empty() ) {
                if ( !isOnlyForPolling( *watchedDirectory ) ) {
                    watcher_.removeWatch( watchedDirectory->watchId );
                }

                watchedPaths_.erase( watchedDirectory );
            }
        }
        else {
            LOG_WARNING << "The file is not watched";
        }

        for ( const auto& d : watcher_.directories() ) {
            LOG_INFO << "Directories still watched: " << d;
        }
    }

    void checkWatches()
    {
        const auto collectChangedFiles = [ this ]() {
            ScopedRecursiveLock lock( mutex_ );

            std::vector<std::string> changedFiles;

            for ( auto& dir : watchedPaths_ ) {
                for ( auto& file : dir.files ) {
                    const auto path = buildWatchPath( dir.name, file.name );

                    std::error_code errorCode;
                    const auto mTime = std::filesystem::last_write_time(
                        platform::paths::pathFromUtf8( path ), errorCode );
                    if ( errorCode ) {
                        // File temporarily gone; keep the current snapshot so a
                        // later appearance is detected on the next poll.
                        continue;
                    }

                    const auto size = std::filesystem::file_size(
                        platform::paths::pathFromUtf8( path ), errorCode );

                    const auto watchedFile = WatchedFile{ file.name, mTime,
                                                          errorCode ? 0 : size };

                    if ( file != watchedFile ) {
                        changedFiles.push_back( path );
                        LOG_INFO << "will notify for " << path;
                    }

                    file = std::move( watchedFile );
                }
            }

            return changedFiles;
        };

        for ( const auto& changedFile : collectChangedFiles() ) {
            notifyChanged( changedFile );
        }
    }

    // Finds the watched file matching the efsw notification and returns its
    // full path, or an empty string when nothing watched changed.
    std::string findChangedFilename( const std::string& directory, const std::string& filename,
                                     const std::string& oldFilename )
    {
        ScopedRecursiveLock lock( mutex_ );

        // efsw may report the directory with native separators; normalize to the
        // generic form used by addFile so the comparison is exact.
        const auto normalizedDirectory
            = platform::paths::pathFromUtf8( directory ).generic_string();

        auto watchedDirectory
            = std::find_if( watchedPaths_.begin(), watchedPaths_.end(),
                            [ &normalizedDirectory ]( const auto& wd ) {
                                return wd.name == normalizedDirectory;
                            } );

        if ( watchedDirectory != watchedPaths_.end() ) {
            std::string changedFilename;

            const auto isFileWatched
                = std::any_of( watchedDirectory->files.begin(), watchedDirectory->files.end(),
                               [ &filename, &oldFilename, &changedFilename ]( const auto& f ) {
                                   if ( f.name == filename || f.name == oldFilename ) {
                                       changedFilename = f.name;
                                       return true;
                                   }

                                   return false;
                               } );

            if ( isFileWatched ) {
                LOG_DEBUG << "fileChangedOnDisk - will notify for " << filename << ", old name "
                          << oldFilename;

                return buildWatchPath( directory, changedFilename );
            }
            LOG_DEBUG << "fileChangedOnDisk - call but no file monitored";
        }
        else {
            LOG_DEBUG << "fileChangedOnDisk - call but no dir monitored";
        }

        return {};
    }

    void handleFileAction( efsw::WatchID watchid, const std::string& dir,
                           const std::string& filename, efsw::Action action,
                           std::string oldFilename ) override
    {
        (void)watchid;
        (void)action;

        LOG_DEBUG << "Notification from esfw for " << dir;

        // post to another thread to avoid deadlock between internal efsw lock
        // and our mutex_; the coordinator serializes watcher state access.
        coordinator_->post( [ this, dir, filename, oldFilename = std::move( oldFilename ) ] {
            notifyOnFileAction( dir, filename, oldFilename );
        } );
    }

    // Runs on the coordinator thread.
    void notifyOnFileAction( const std::string& dir, const std::string& filename,
                             const std::string& oldFilename )
    {
        const auto directory = stripTrailingSeparator( dir );

        LOG_DEBUG << "fileChangedOnDisk " << directory << " " << filename << ", old name "
                  << oldFilename;

        const auto fullChangedFilename = findChangedFilename( directory, filename, oldFilename );

        if ( !fullChangedFilename.empty() ) {
            notifyChanged( fullChangedFilename );
        }
    }

    // Runs on the coordinator thread.
    void notifyChanged( const std::string& path )
    {
        onFileChanged_( path );
    }

    void setOnFileChanged( std::function<void( const std::string& )> callback )
    {
        onFileChanged_ = std::move( callback );
    }

  private:
    efsw::FileWatcher watcher_;
    std::vector<WatchedDirecotry> watchedPaths_;
    klogg::SessionExecutor* coordinator_;

    std::function<void( const std::string& )> onFileChanged_;

    bool nativeWatchEnabled_ = true;

    RecursiveMutex mutex_;
};

void EfswFileWatcherDeleter::operator()( EfswFileWatcher* watcher ) const
{
    delete watcher;
}

FileWatcher::FileWatcher()
    : coordinator_( std::make_unique<klogg::SessionExecutor>( "klogg-filewatcher" ) )
    , throttler_( std::make_unique<klogg::TrailingThrottler>(
          kThrottleWindow, [ this ] { sendChangesNotifications(); } ) )
    , efswWatcher_( new EfswFileWatcher( coordinator_.get() ) )
{
    efswWatcher_->setOnFileChanged( [ this ]( const std::string& path ) {
        notifyFileChanged( path );
    } );

#ifdef _WIN32
    pollingEnabled_.store( true );
#else
    pollingEnabled_.store( false );
#endif
    pollIntervalMs_.store( 2000 );

    pollThread_ = std::thread( &FileWatcher::pollingLoop, this );
}

FileWatcher::~FileWatcher()
{
    stopPolling();
    coordinator_->stop();
    throttler_.reset();
    efswWatcher_.reset();
}

FileWatcher& FileWatcher::getFileWatcher()
{
    static auto* const instance = new FileWatcher;
    return *instance;
}

void FileWatcher::addFile( const std::string& fileName )
{
    coordinator_->post( [ this, fileName ] { addFileOnCoordinator( fileName ); } );
}

void FileWatcher::addFileOnCoordinator( const std::string& fileName )
{
    efswWatcher_->addFile( fileName );
    enableWatchOnCoordinator( nativeFileWatchEnabled_.load() );
}

void FileWatcher::removeFile( const std::string& fileName )
{
    coordinator_->post( [ this, fileName ] { removeFileOnCoordinator( fileName ); } );
}

void FileWatcher::removeFileOnCoordinator( const std::string& fileName )
{
    efswWatcher_->removeFile( fileName );
}

// Trailing throttle window: notifications are merged and sent once after
// 250 ms of quiet.
void FileWatcher::notifyFileChanged( const std::string& fileName )
{
    {
        std::lock_guard lock( changesMutex_ );
        if ( std::find( changes_.begin(), changes_.end(), fileName ) == changes_.end() ) {
            changes_.push_back( fileName );
        }
    }
    throttler_->trigger();
}

// Runs on the throttler timer thread; delivers each change to every observer
// through its own PostTarget, so the callbacks run on the registering threads.
void FileWatcher::sendChangesNotifications()
{
    std::vector<std::string> filesToNotify;
    {
        std::lock_guard lock( changesMutex_ );
        filesToNotify.swap( changes_ );
    }

    std::vector<Observer> observers;
    {
        std::lock_guard lock( observersMutex_ );
        observers = observers_;
    }

    for ( const auto& fileName : filesToNotify ) {
        for ( const auto& observer : observers ) {
            if ( !observer.postTarget ) {
                continue;
            }
            // The callback copy keeps the observer's closure alive even if
            // removeObserver happens after the snapshot was taken.
            observer.postTarget( [ callback = observer.callback, fileName ]() {
                callback( fileName );
            } );
        }
    }
}

uint64_t FileWatcher::addObserver( ChangeCallback callback, PostFunction postTarget )
{
    std::lock_guard lock( observersMutex_ );
    const auto id = nextObserverId_++;
    observers_.push_back( Observer{ id, std::move( callback ), std::move( postTarget ) } );
    return id;
}

void FileWatcher::removeObserver( uint64_t observerId )
{
    std::lock_guard lock( observersMutex_ );
    observers_.erase(
        std::remove_if( observers_.begin(), observers_.end(),
                        [ observerId ]( const auto& observer ) { return observer.id == observerId; } ),
        observers_.end() );
}

void FileWatcher::setConfiguration( const Options& options )
{
    if ( options.pollingEnabled ) {
        LOG_INFO << "Polling files enabled";
    }
    else {
        LOG_INFO << "Polling files disabled";
    }

    pollingEnabled_.store( options.pollingEnabled );
    pollIntervalMs_.store( options.pollIntervalMs );
    nativeFileWatchEnabled_.store( options.nativeFileWatchEnabled );
    pollCv_.notify_all();

    coordinator_->post( [ this, enable = options.nativeFileWatchEnabled ] {
        enableWatchOnCoordinator( enable );
    } );
}

void FileWatcher::enableWatchOnCoordinator( bool enable )
{
    efswWatcher_->enableWatch( enable );
}

void FileWatcher::checkWatchesOnCoordinator()
{
    efswWatcher_->checkWatches();
}

// Sleeping ticker: wakes on configuration changes so a shorter poll interval
// applies on the next cycle (and a longer one without an extra stat).
void FileWatcher::pollingLoop()
{
    std::unique_lock lock( pollMutex_ );
    while ( !stopPolling_.load() ) {
        if ( !pollingEnabled_.load() || pollIntervalMs_.load() <= 0 ) {
            pollCv_.wait( lock, [ this ] {
                return stopPolling_.load()
                       || ( pollingEnabled_.load() && pollIntervalMs_.load() > 0 );
            } );
            continue;
        }

        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds( pollIntervalMs_.load() );

        if ( pollCv_.wait_until( lock, deadline ) == std::cv_status::timeout
             && !stopPolling_.load() ) {
            coordinator_->post( [ this ] { checkWatchesOnCoordinator(); } );
        }
    }
}

void FileWatcher::stopPolling()
{
    stopPolling_.store( true );
    pollCv_.notify_all();
    if ( pollThread_.joinable() ) {
        pollThread_.join();
    }
}
