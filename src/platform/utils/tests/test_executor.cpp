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

#include <catch2/catch.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "executor.h"

using namespace std::chrono_literals;

TEST_CASE( "SessionExecutor executes posted tasks in FIFO order" )
{
    klogg::SessionExecutor executor;
    std::vector<int> order;

    for ( int i = 0; i < 100; ++i ) {
        executor.post( [i, &order] { order.push_back( i ); } );
    }

    executor.stop();
    REQUIRE( order.size() == 100 );
    for ( int i = 0; i < 100; ++i ) {
        REQUIRE( order[ static_cast<size_t>( i ) ] == i );
    }
}

TEST_CASE( "SessionExecutor dispatch returns the task result" )
{
    klogg::SessionExecutor executor;

    REQUIRE( executor.dispatch( [] { return 42; } ) == 42 );
    REQUIRE( executor.dispatch( [] { return std::string( "hello" ); } ) == "hello" );

    const std::vector<int> vectorResult
        = executor.dispatch( [] { return std::vector<int>{ 1, 2, 3 }; } );
    REQUIRE( vectorResult.size() == 3 );
    REQUIRE( vectorResult[ 0 ] == 1 );
    REQUIRE( vectorResult[ 1 ] == 2 );
    REQUIRE( vectorResult[ 2 ] == 3 );

    bool ran = false;
    executor.dispatch( [&ran] { ran = true; } );
    REQUIRE( ran );

    executor.stop();
}

TEST_CASE( "SessionExecutor dispatch on the executor thread runs inline" )
{
    klogg::SessionExecutor executor;

    std::promise<std::tuple<std::thread::id, int, bool>> done;
    auto doneFuture = done.get_future();

    std::thread dispatcher( [&] {
        executor.dispatch( [&] {
            const std::thread::id taskThread = std::this_thread::get_id();
            // A queued nested dispatch would block forever: this task sits on
            // the executor thread and nobody would serve the queue. The
            // wait_for below turns that into a timeout failure.
            const int nested = executor.dispatch( [] { return 7; } );
            done.set_value( { taskThread, nested, executor.isExecutorThread() } );
        } );
    } );

    REQUIRE( doneFuture.wait_for( 5s ) == std::future_status::ready );
    const auto [ taskThread, nested, onExecutor ] = doneFuture.get();
    REQUIRE( onExecutor );
    REQUIRE( nested == 7 );
    REQUIRE( taskThread != std::this_thread::get_id() );

    dispatcher.join();
    executor.stop();
}

TEST_CASE( "SessionExecutor pumpUntil runs tasks posted from inside a task" )
{
    klogg::SessionExecutor executor;

    std::atomic<bool> flag{ false };
    std::promise<bool> result;
    auto resultFuture = result.get_future();

    executor.post( [&] {
        executor.post( [&flag] { flag = true; } );
        // The worker is stuck in this pump; the task posted above only runs
        // if the pump services the queue itself.
        result.set_value( executor.pumpUntil( [&flag] { return flag.load(); }, 2s ) );
    } );

    REQUIRE( resultFuture.wait_for( 5s ) == std::future_status::ready );
    REQUIRE( resultFuture.get() );
    executor.stop();
}

TEST_CASE( "SessionExecutor pumpUntil runs dispatches from other threads" )
{
    klogg::SessionExecutor executor;

    std::atomic<bool> dispatched{ false };
    std::atomic<bool> pumpStarted{ false };
    std::atomic<int> dispatchResult{ 0 };
    std::promise<bool> pumpResult;
    auto pumpFuture = pumpResult.get_future();

    executor.post( [&] {
        pumpStarted = true;
        pumpResult.set_value( executor.pumpUntil( [&dispatched] { return dispatched.load(); }, 5s ) );
    } );

    const auto spinDeadline = std::chrono::steady_clock::now() + 5s;
    while ( !pumpStarted.load() && std::chrono::steady_clock::now() < spinDeadline ) {
        std::this_thread::yield();
    }
    REQUIRE( pumpStarted.load() );

    std::thread dispatcher( [&] {
        dispatchResult.store( executor.dispatch( [&dispatched] {
            dispatched = true;
            return 3;
        } ) );
    } );

    // A buggy pump would time out after 5s and report false; the dispatcher
    // would then still be served by the normal worker loop, so nothing hangs.
    REQUIRE( pumpFuture.wait_for( 10s ) == std::future_status::ready );
    REQUIRE( pumpFuture.get() );
    dispatcher.join();
    REQUIRE( dispatchResult.load() == 3 );

    executor.stop();
}

TEST_CASE( "SessionExecutor pumpUntil times out when the predicate stays false" )
{
    klogg::SessionExecutor executor;

    std::promise<std::chrono::milliseconds> elapsedPromise;
    auto elapsedFuture = elapsedPromise.get_future();

    executor.post( [&] {
        const auto start = std::chrono::steady_clock::now();
        const bool result = executor.pumpUntil( [] { return false; }, 50ms );
        elapsedPromise.set_value( result ? std::chrono::milliseconds( 0 )
                                         : std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now() - start ) );
    } );

    REQUIRE( elapsedFuture.wait_for( 5s ) == std::future_status::ready );
    const auto elapsed = elapsedFuture.get();
    REQUIRE( elapsed >= 50ms );
    REQUIRE( elapsed < 5s );
    executor.stop();
}

TEST_CASE( "SessionExecutor pumpUntil succeeds immediately when the predicate already holds" )
{
    klogg::SessionExecutor executor;

    const auto start = std::chrono::steady_clock::now();
    REQUIRE( executor.pumpUntil( [] { return true; }, 10ms ) );
    REQUIRE( std::chrono::steady_clock::now() - start < 250ms );
    executor.stop();
}

TEST_CASE( "SessionExecutor stop drains the queue, drops new tasks and is idempotent" )
{
    klogg::SessionExecutor executor;

    std::atomic<int> ran{ 0 };
    for ( int i = 0; i < 64; ++i ) {
        executor.post( [&ran] { ++ran; } );
    }
    executor.stop();
    REQUIRE( ran.load() == 64 );

    // Dropped silently after stop.
    executor.post( [&ran] { ++ran; } );
    REQUIRE( ran.load() == 64 );

    // A dispatch after stop is dropped and yields a default-constructed result.
    REQUIRE( executor.dispatch( [] { return 99; } ) == 0 );

    executor.stop();
    REQUIRE( ran.load() == 64 );
}

TEST_CASE( "SessionExecutor destructor stops the worker and drains the queue" )
{
    std::atomic<int> ran{ 0 };
    {
        klogg::SessionExecutor executor;
        for ( int i = 0; i < 16; ++i ) {
            executor.post( [&ran] { ++ran; } );
        }
    }
    REQUIRE( ran.load() == 16 );
}

TEST_CASE( "SessionExecutor serializes tasks from multiple producer threads" )
{
    klogg::SessionExecutor executor;

    constexpr int producerCount = 4;
    constexpr int tasksPerProducer = 250;
    std::vector<int> slots( static_cast<size_t>( producerCount * tasksPerProducer ), 0 );
    std::atomic<int> ran{ 0 };

    std::vector<std::thread> producerThreads;
    producerThreads.reserve( static_cast<size_t>( producerCount ) );
    for ( int producer = 0; producer < producerCount; ++producer ) {
        producerThreads.emplace_back( [&, producer] {
            for ( int i = 0; i < tasksPerProducer; ++i ) {
                executor.post( [&slots, &ran, slot = producer * tasksPerProducer + i] {
                    ++slots[ static_cast<size_t>( slot ) ];
                    ++ran;
                } );
            }
        } );
    }

    for ( auto& thread : producerThreads ) {
        thread.join();
    }
    executor.stop();

    REQUIRE( ran.load() == producerCount * tasksPerProducer );
    for ( const int count : slots ) {
        REQUIRE( count == 1 );
    }
}

TEST_CASE( "SessionExecutor isExecutorThread identifies the worker thread only" )
{
    klogg::SessionExecutor executor;
    REQUIRE_FALSE( executor.isExecutorThread() );

    std::promise<bool> inWorker;
    auto inWorkerFuture = inWorker.get_future();
    executor.post( [&] { inWorker.set_value( executor.isExecutorThread() ); } );
    REQUIRE( inWorkerFuture.wait_for( 5s ) == std::future_status::ready );
    REQUIRE( inWorkerFuture.get() );

    executor.stop();
    REQUIRE_FALSE( executor.isExecutorThread() );
}

TEST_CASE( "SessionExecutor survives a posted task that throws" )
{
    klogg::SessionExecutor executor;
    std::atomic<int> ran{ 0 };

    executor.post( [] { throw std::runtime_error( "posted task failure" ); } );
    executor.post( [&ran] { ++ran; } );

    // An exception escaping the worker loop would terminate the process.
    executor.stop();
    REQUIRE( ran.load() == 1 );
}

TEST_CASE( "SessionExecutor dispatch returns a default result for a throwing task" )
{
    klogg::SessionExecutor executor;

    std::promise<int> result;
    auto resultFuture = result.get_future();
    std::thread dispatcher( [&] {
        result.set_value( executor.dispatch( []() -> int {
            throw std::runtime_error( "dispatch task failure" );
        } ) );
    } );

    // Without the guard this either kills the process (exception out of the
    // worker thread) or blocks here forever (completion flag never set).
    REQUIRE( resultFuture.wait_for( 5s ) == std::future_status::ready );
    REQUIRE( resultFuture.get() == 0 );

    // The worker survived and still serves the queue.
    REQUIRE( executor.dispatch( [] { return 11; } ) == 11 );

    dispatcher.join();
    executor.stop();
}

TEST_CASE( "SessionExecutor pumpUntil survives a task that throws" )
{
    klogg::SessionExecutor executor;

    std::atomic<bool> laterTaskRan{ false };
    std::promise<bool> pumpResult;
    auto pumpFuture = pumpResult.get_future();

    executor.post( [&] {
        executor.post( [] { throw std::runtime_error( "pump task failure" ); } );
        executor.post( [&laterTaskRan] { laterTaskRan = true; } );
        pumpResult.set_value(
            executor.pumpUntil( [&laterTaskRan] { return laterTaskRan.load(); }, 2s ) );
    } );

    // The pump drains the queue inline on the executor thread, so the throwing
    // task runs there and must not take the worker down with it.
    REQUIRE( pumpFuture.wait_for( 5s ) == std::future_status::ready );
    REQUIRE( pumpFuture.get() );
    REQUIRE( laterTaskRan.load() );
    executor.stop();
}
