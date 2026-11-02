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

#include "throttler.h"

#include <exception>
#include <utility>

#include "log.h"

namespace klogg {

namespace {

// The callback runs on the timer thread, or on the destroying thread during
// the destructor flush: an escaping exception would terminate the process.
void runCallbackGuarded( const TrailingThrottler::Callback& callback )
{
    try {
        callback();
    }
    catch ( const std::exception& error ) {
        LOG_ERROR << "TrailingThrottler callback failed: " << error.what();
    }
    catch ( ... ) {
        LOG_ERROR << "TrailingThrottler callback failed: unknown exception";
    }
}

} // namespace

TrailingThrottler::TrailingThrottler( std::chrono::milliseconds windowDuration, Callback callback )
    : windowDuration_( windowDuration )
    , callback_( std::move( callback ) )
{
    timerThread_ = std::thread( [this] { timerLoop(); } );
}

TrailingThrottler::~TrailingThrottler()
{
    {
        std::lock_guard<std::mutex> guard( mutex_ );
        stopping_ = true;
    }
    cv_.notify_all();
    if ( timerThread_.joinable() ) {
        timerThread_.join();
    }
    flush();
}

void TrailingThrottler::trigger()
{
    std::lock_guard<std::mutex> guard( mutex_ );
    pending_ = true;
    if ( !windowActive_ ) {
        windowActive_ = true;
        windowEnd_ = std::chrono::steady_clock::now() + windowDuration_;
    }
    cv_.notify_all();
}

void TrailingThrottler::timerLoop()
{
    std::unique_lock<std::mutex> lock( mutex_ );
    while ( true ) {
        if ( !windowActive_ ) {
            cv_.wait( lock, [this] { return stopping_ || windowActive_; } );
            if ( stopping_ ) {
                return;
            }
            continue;
        }

        cv_.wait_until( lock, windowEnd_, [this] { return stopping_ || !windowActive_; } );
        if ( stopping_ ) {
            return;
        }
        if ( !windowActive_ ) {
            continue;
        }

        // Close the window before running the callback so a trigger() inside
        // the callback opens a fresh window (direct-connection semantics).
        windowActive_ = false;
        const bool emit = pending_;
        pending_ = false;
        lock.unlock();
        if ( emit ) {
            runCallbackGuarded( callback_ );
        }
        lock.lock();
    }
}

void TrailingThrottler::flush()
{
    std::unique_lock<std::mutex> lock( mutex_ );
    const bool emit = pending_;
    pending_ = false;
    lock.unlock();
    if ( emit ) {
        runCallbackGuarded( callback_ );
    }
}

} // namespace klogg
