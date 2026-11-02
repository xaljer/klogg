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

#if defined(__linux__) && !defined(_GNU_SOURCE)
// pthread_setname_np is only declared with _GNU_SOURCE on glibc < 2.32;
// feature-test macros must precede any system header.
#define _GNU_SOURCE
#endif

#include "executor.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

#include "log.h"

namespace klogg {

namespace {

void setThreadName( const std::string& name )
{
#if defined(_WIN32)
    const std::wstring wideName( name.begin(), name.end() );
    SetThreadDescription( GetCurrentThread(), wideName.c_str() );
#elif defined(__linux__)
    pthread_setname_np( pthread_self(), name.c_str() );
#elif defined(__APPLE__)
    pthread_setname_np( name.c_str() );
#else
    (void)name;
#endif
}

// Every task execution point funnels through here: an exception escaping into
// a thread entry point (or out of a destructor) would hit std::terminate and
// take the whole process down.
void runTaskGuarded( const SessionExecutor::Task& task, const char* origin )
{
    try {
        task();
    }
    catch ( const std::exception& error ) {
        LOG_ERROR << "Task failed in SessionExecutor::" << origin << ": " << error.what();
    }
    catch ( ... ) {
        LOG_ERROR << "Task failed in SessionExecutor::" << origin << ": unknown exception";
    }
}

} // namespace

class SessionExecutor::Impl
{
  public:
    explicit Impl( std::string threadName )
        : threadName_( std::move( threadName ) )
    {
    }

    void workerLoop()
    {
        {
            std::lock_guard<std::mutex> guard( mutex_ );
            threadId_ = std::this_thread::get_id();
        }
        setThreadName( threadName_ );

        std::unique_lock<std::mutex> lock( mutex_ );
        while ( true ) {
            // Wait until there is work to do or shutdown starts.
            cv_.wait( lock, [this] { return !tasks_.empty() || stopping_; } );
            if ( tasks_.empty() ) {
                // Stopping with an empty queue: nothing left to drain.
                return;
            }

            Task task = std::move( tasks_.front() );
            tasks_.pop_front();
            lock.unlock();
            runTaskGuarded( task, "post" );
            lock.lock();
        }
    }

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> tasks_;
    bool stopping_ = false;
    std::thread::id threadId_;
    std::string threadName_;
};

SessionExecutor::SessionExecutor( std::string threadName )
    : impl_( std::make_unique<Impl>( std::move( threadName ) ) )
{
    Impl* impl = impl_.get();
    impl_->thread_ = std::thread( [impl] { impl->workerLoop(); } );
}

SessionExecutor::~SessionExecutor()
{
    if ( impl_ ) {
        stop();
    }
}

SessionExecutor::SessionExecutor( SessionExecutor&& other ) noexcept
    : impl_( std::move( other.impl_ ) )
{
}

SessionExecutor& SessionExecutor::operator=( SessionExecutor&& other ) noexcept
{
    if ( this != &other ) {
        if ( impl_ ) {
            stop();
        }
        impl_ = std::move( other.impl_ );
    }
    return *this;
}

void SessionExecutor::post( Task task )
{
    std::lock_guard<std::mutex> guard( impl_->mutex_ );
    if ( impl_->stopping_ ) {
        return;
    }
    impl_->tasks_.push_back( std::move( task ) );
    impl_->cv_.notify_all();
}

void SessionExecutor::dispatchAsync( Task task )
{
    auto state = std::make_shared<bool>( false );
    std::unique_lock<std::mutex> lock( impl_->mutex_ );
    if ( impl_->stopping_ ) {
        // Dropped, like post() after stop(); the caller gets a default result.
        return;
    }
    impl_->tasks_.push_back( [ task = std::move( task ), state, impl = impl_.get() ] {
        // The guard swallows task exceptions, so the completion flag below is
        // reached on every path - a throwing task must not strand the waiter.
        runTaskGuarded( task, "dispatch" );
        {
            std::lock_guard<std::mutex> guard( impl->mutex_ );
            *state = true;
            impl->cv_.notify_all();
        }
    } );
    impl_->cv_.notify_all();
    impl_->cv_.wait( lock, [ &state ] { return *state; } );
}

bool SessionExecutor::pumpUntil( Predicate predicate, std::chrono::milliseconds timeout )
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const bool onExecutor = isExecutorThread();
    std::unique_lock<std::mutex> lock( impl_->mutex_ );

    while ( true ) {
        // The predicate runs without the queue lock: it may legitimately call
        // post()/dispatch() which either enqueue (foreign thread) or run
        // inline (executor thread).
        lock.unlock();
        if ( predicate() ) {
            return true;
        }
        lock.lock();

        if ( impl_->stopping_ || std::chrono::steady_clock::now() >= deadline ) {
            return false;
        }

        if ( onExecutor && !impl_->tasks_.empty() ) {
            // Nested-loop semantics: the executor thread sits in the pump, so it
            // must run the queue itself to make progress.
            Task task = std::move( impl_->tasks_.front() );
            impl_->tasks_.pop_front();
            lock.unlock();
            runTaskGuarded( task, "pumpUntil" );
            lock.lock();
            continue;
        }

        // Every queue mutation (post, dispatch, task completion, stop) issues
        // notify_all, so a plain timed wait re-checks the predicate whenever
        // anything happened without busy-spinning on a non-empty queue.
        impl_->cv_.wait_until( lock, deadline );
    }
}

void SessionExecutor::stop()
{
    std::thread thread;
    {
        std::lock_guard<std::mutex> guard( impl_->mutex_ );
        if ( impl_->stopping_ ) {
            return;
        }
        impl_->stopping_ = true;
        impl_->cv_.notify_all();
        // Move the worker out so the join below does not run while holding
        // the mutex: the worker needs the mutex to observe stopping_ and exit.
        thread = std::move( impl_->thread_ );
    }
    if ( thread.joinable() ) {
        thread.join();
    }
}

bool SessionExecutor::isExecutorThread() const noexcept
{
    std::lock_guard<std::mutex> guard( impl_->mutex_ );
    return std::this_thread::get_id() == impl_->threadId_;
}

} // namespace klogg
