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

#ifndef KLOGG_EXECUTOR_H
#define KLOGG_EXECUTOR_H

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace klogg {

// Single-threaded FIFO task executor (one worker thread + MPSC queue), the
// Qt-free replacement for the QThread + BlockingQueued pattern used by the
// Session worker. Every task runs on the executor thread; serialized
// execution is what makes the engine state safe to touch from the tasks.
//
// - post(): fire-and-forget
// - dispatch(): blocks for the result; runs inline when the caller already
//   sits on the executor thread (the iron rule that lets callbacks call back
//   into the API without self-deadlock)
// - pumpUntil(): services the whole queue while waiting, replicating the
//   nested QEventLoop behaviour of the current worker
// - stop(): deterministic shutdown - stops accepting tasks, drains the
//   queue, joins the worker thread; the destructor stops implicitly
//
// Exception contract: a task that throws never escapes the executor. The
// exception is caught at the execution point, logged with LOG_ERROR and
// dropped - the worker thread and the process survive. A dispatch() onto a
// throwing task therefore still completes: the waiting caller receives a
// default-constructed result instead of a crash or an endless wait. Only an
// inline dispatch (caller already on the executor thread) lets the exception
// propagate into its enclosing task, which is guarded the same way.
class SessionExecutor
{
  public:
    using Task = std::function<void()>;
    using Predicate = std::function<bool()>;

    // threadName is set on the worker thread via SetThreadDescription
    // (Windows) or pthread_setname_np (Linux/macOS); a debug aid only.
    explicit SessionExecutor( std::string threadName = "klogg-session" );
    ~SessionExecutor();

    SessionExecutor( SessionExecutor&& ) noexcept;
    SessionExecutor& operator=( SessionExecutor&& ) noexcept;

    SessionExecutor( const SessionExecutor& ) = delete;
    SessionExecutor& operator=( const SessionExecutor& ) = delete;

    // Queues a task and returns immediately. Tasks queued after stop() are
    // dropped; anything queued before stop() still runs (drain semantics).
    void post( Task task );

    // Runs fn on the executor thread and blocks until it returns its result.
    // When the caller is already on the executor thread, fn executes inline
    // within the current task (no queue round-trip, no deadlock). A dispatch
    // issued after stop() is dropped and a default-constructed result is
    // returned - same observable outcome as the QThread BlockingQueued path
    // after the worker loop quits. A throwing fn is logged and the caller
    // still unblocks with a default-constructed result (see the exception
    // contract above); from the executor thread it propagates into the
    // enclosing task instead.
    template <typename Fn>
    auto dispatch( Fn&& fn ) -> std::invoke_result_t<Fn>
    {
        using R = std::invoke_result_t<Fn>;
        if ( isExecutorThread() ) {
            return std::invoke( std::forward<Fn>( fn ) );
        }

        if constexpr ( std::is_void_v<R> ) {
            dispatchAsync( [ fn = std::forward<Fn>( fn ) ]() mutable { std::invoke( fn ); } );
        }
        else {
            R result{};
            dispatchAsync( [ &fn, &result ] { result = std::invoke( fn ); } );
            return result;
        }
    }

    // Waits until predicate() returns true (-> true) or timeout elapses
    // (-> false; true may still win if the predicate flips exactly on the
    // deadline). While the caller is on the executor thread the pending
    // queue is run inline between predicate checks, so task T waiting on,
    // say, a completion posted by a foreign thread can still be serviced;
    // from any other thread the worker keeps servicing the queue and the
    // call simply waits passively.
    bool pumpUntil( Predicate predicate, std::chrono::milliseconds timeout );

    // Stops accepting tasks, drains the remaining queue and joins the worker
    // thread; idempotent, also called from the destructor. Must not be
    // called from the executor thread itself (joining self would throw).
    void stop();

    bool isExecutorThread() const noexcept;

  private:
    // Blocking enqueue used by dispatch(): the task carries a completion
    // state that is set after it ran, and this call waits on it.
    void dispatchAsync( Task task );

    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace klogg

#endif
