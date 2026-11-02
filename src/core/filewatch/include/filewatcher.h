/*
 * Copyright (C) 2010, 2014 Nicolas Bonnefon and other contributors
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

#ifndef FILEWATCHER_H
#define FILEWATCHER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "posttarget.h"

class EfswFileWatcher;

namespace klogg {
class SessionExecutor;
class TrailingThrottler;
}

struct EfswFileWatcherDeleter {
    void operator()( EfswFileWatcher* p ) const;
};

// Watches files for changes on disk and notifies the registered observers.
//
// The internal coordinator thread serializes all watcher state access (the
// efsw thread only posts to it, keeping the callbacks lock-free); a poll
// thread stats the watched files every pollIntervalMs; the notifications are
// merged in a trailing throttle window and then delivered to every observer
// through its own PostTarget (each callback runs on the registering thread).
class FileWatcher {
  public:
    using ChangeCallback = std::function<void( const std::string& )>;

    // Posts the notification task into the thread owning the observer; the
    // target must not run the task inline. Same type as the core PostTarget.
    using PostFunction = PostTarget;

    // Process-level watcher configuration, injected by the composition root
    // so the watcher never reads a global configuration.
    struct Options {
        bool nativeFileWatchEnabled = true;
#ifdef _WIN32
        bool pollingEnabled = true;
#else
        bool pollingEnabled = false;
#endif
        int pollIntervalMs = 2000;
    };

    FileWatcher( const FileWatcher& ) = delete;
    FileWatcher( FileWatcher&& ) = delete;

    FileWatcher& operator=( const FileWatcher& ) = delete;
    FileWatcher& operator=( FileWatcher&& ) = delete;

    static FileWatcher& getFileWatcher();

    // Adds the file to the list of file to watch
    // (do nothing if a file is already monitored)
    void addFile( const std::string& fileName );

    // Removes the file to the list of file to watch
    // (do nothing if said file is not monitored)
    void removeFile( const std::string& fileName );

    void setConfiguration( const Options& options );

    // Registers an observer; the callback receives the path of the changed
    // file, delivered via postTarget on the registering thread. Returns an id
    // to pass to removeObserver.
    uint64_t addObserver( ChangeCallback callback, PostFunction postTarget );

    // Stops new notifications for the observer; notifications already queued
    // on the postTarget are still delivered (the observer must guard itself).
    void removeObserver( uint64_t observerId );

  private:
    FileWatcher();
    ~FileWatcher(); // for complete EfswFileWatcher

    // All of these run on the coordinator thread.
    void addFileOnCoordinator( const std::string& fileName );
    void removeFileOnCoordinator( const std::string& fileName );
    void enableWatchOnCoordinator( bool enable );
    void checkWatchesOnCoordinator();
    void notifyFileChanged( const std::string& fileName );

    // Runs on the throttler timer thread.
    void sendChangesNotifications();

    void pollingLoop();
    void stopPolling();

    // Declaration order is init order: the coordinator must exist before the
    // efsw engine (the efsw callbacks post to it).
    std::unique_ptr<klogg::SessionExecutor> coordinator_;
    std::unique_ptr<klogg::TrailingThrottler> throttler_;
    std::unique_ptr<EfswFileWatcher, EfswFileWatcherDeleter> efswWatcher_;

    std::vector<std::string> changes_;
    std::mutex changesMutex_;

    struct Observer {
        uint64_t id;
        ChangeCallback callback;
        PostFunction postTarget;
    };
    std::vector<Observer> observers_;
    std::mutex observersMutex_;
    uint64_t nextObserverId_ = 1;

    std::thread pollThread_;
    std::mutex pollMutex_;
    std::condition_variable pollCv_;
    std::atomic<bool> stopPolling_{ false };
    std::atomic<bool> pollingEnabled_{ false };
    std::atomic<int> pollIntervalMs_{ 2000 };
    std::atomic<bool> nativeFileWatchEnabled_{ true };
};

#endif
