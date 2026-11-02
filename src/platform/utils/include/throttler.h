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

#ifndef KLOGG_THROTTLER_H
#define KLOGG_THROTTLER_H

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace klogg {

// Trailing-edge signal throttler, semantically identical to KDToolBox
// KDSignalThrottler's Trailing mode:
// - the first trigger() opens a window; every trigger() inside it marks the
//   emission pending but leaves the window end untouched (no restart)
// - when the window ends, one emission fires if anything was pending; pending
//   state resets, so a burst of N triggers inside one window produces exactly
//   one emission at the window end (the last state wins, not the last call)
// - each new burst re-opens a window after the quiet gap
//
// The callback runs on the executor's internal timer thread; the only
// exception is the destructor flush, which runs on the destroying thread. The
// consumer is responsible for marshalling the callback onto its own thread
// (the engine convention - same as C4's eventSink). A callback that calls
// trigger() again re-opens a window immediately (direct-connection semantics).
// Zero or negative durations emit on the next timer wake-up after trigger().
//
// trigger() is the only thread-safe entry point next to the destructor; the
// destructor joins the timer thread before flushing, so with a concurrent
// trigger the emission either happened inside the window or happens exactly
// once in the flush - never twice, never dropped.
//
// Exception contract: a throwing callback never escapes. Both emission points
// (timer thread, destructor flush) catch it, log it with LOG_ERROR and carry
// on - the throttler keeps working and the process does not terminate.
class TrailingThrottler
{
  public:
    using Callback = std::function<void()>;

    TrailingThrottler( std::chrono::milliseconds windowDuration, Callback callback );
    ~TrailingThrottler();

    TrailingThrottler( const TrailingThrottler& ) = delete;
    TrailingThrottler& operator=( const TrailingThrottler& ) = delete;

    // Marks an emission pending and opens the window if none is active.
    void trigger();

  private:
    void timerLoop();
    void flush();

    std::chrono::milliseconds windowDuration_;
    Callback callback_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread timerThread_;
    bool stopping_ = false;
    bool pending_ = false;
    bool windowActive_ = false;
    std::chrono::steady_clock::time_point windowEnd_;
};

} // namespace klogg

#endif
