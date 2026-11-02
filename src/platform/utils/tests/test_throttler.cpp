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

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include "throttler.h"

using namespace std::chrono_literals;

namespace {

class EmissionRecorder
{
  public:
    void record( std::thread::id threadId = std::this_thread::get_id() )
    {
        std::lock_guard<std::mutex> guard( mutex_ );
        emissions_.push_back( std::chrono::steady_clock::now() );
        threads_.push_back( threadId );
    }

    size_t count() const
    {
        std::lock_guard<std::mutex> guard( mutex_ );
        return emissions_.size();
    }

    std::optional<std::chrono::steady_clock::time_point> firstEmission() const
    {
        std::lock_guard<std::mutex> guard( mutex_ );
        if ( emissions_.empty() ) {
            return std::nullopt;
        }
        return emissions_.front();
    }

    std::optional<std::thread::id> firstThread() const
    {
        std::lock_guard<std::mutex> guard( mutex_ );
        if ( threads_.empty() ) {
            return std::nullopt;
        }
        return threads_.front();
    }

  private:
    mutable std::mutex mutex_;
    std::vector<std::chrono::steady_clock::time_point> emissions_;
    std::vector<std::thread::id> threads_;
};

bool waitForCount( const EmissionRecorder& recorder, size_t expected,
                   std::chrono::steady_clock::time_point deadline )
{
    while ( std::chrono::steady_clock::now() < deadline ) {
        if ( recorder.count() >= expected ) {
            return true;
        }
        std::this_thread::sleep_for( 1ms );
    }
    return recorder.count() >= expected;
}

} // namespace

TEST_CASE( "TrailingThrottler emits once at the end of the window" )
{
    EmissionRecorder recorder;
    klogg::TrailingThrottler throttler( 200ms, [&recorder] { recorder.record(); } );

    const auto start = std::chrono::steady_clock::now();
    throttler.trigger();

    REQUIRE( waitForCount( recorder, 1, start + 1500ms ) );
    const auto emission = recorder.firstEmission();
    REQUIRE( emission.has_value() );
    REQUIRE( *emission - start >= 150ms );
    REQUIRE( *emission - start < 1500ms );
}

TEST_CASE( "TrailingThrottler does not restart the window" )
{
    EmissionRecorder recorder;
    klogg::TrailingThrottler throttler( 200ms, [&recorder] { recorder.record(); } );

    const auto start = std::chrono::steady_clock::now();
    throttler.trigger();
    std::this_thread::sleep_for( 80ms );
    throttler.trigger();

    REQUIRE( waitForCount( recorder, 1, start + 700ms ) );
    const auto emission = recorder.firstEmission();
    REQUIRE( emission.has_value() );
    // Restarting the window on the second trigger would fire at ~280ms.
    REQUIRE( *emission - start >= 150ms );
    REQUIRE( *emission - start < 260ms );
}

TEST_CASE( "TrailingThrottler coalesces a burst of triggers into one emission" )
{
    EmissionRecorder recorder;
    klogg::TrailingThrottler throttler( 150ms, [&recorder] { recorder.record(); } );

    const auto start = std::chrono::steady_clock::now();
    for ( int i = 0; i < 5; ++i ) {
        throttler.trigger();
        std::this_thread::sleep_for( 20ms );
    }

    REQUIRE( waitForCount( recorder, 1, start + 800ms ) );
    const auto emission = recorder.firstEmission();
    REQUIRE( emission.has_value() );
    REQUIRE( *emission - start >= 120ms );
    // A restarting window would fire after the last trigger, at ~230ms.
    REQUIRE( *emission - start < 220ms );
}

TEST_CASE( "TrailingThrottler emits once per quiet gap" )
{
    EmissionRecorder recorder;
    klogg::TrailingThrottler throttler( 100ms, [&recorder] { recorder.record(); } );

    const auto start = std::chrono::steady_clock::now();
    throttler.trigger();
    REQUIRE( waitForCount( recorder, 1, start + 500ms ) );

    throttler.trigger();
    REQUIRE( waitForCount( recorder, 2, start + 1500ms ) );
    REQUIRE( recorder.count() == 2 );

    const auto first = recorder.firstEmission();
    REQUIRE( first.has_value() );
    REQUIRE( *first - start >= 100ms );
}

TEST_CASE( "TrailingThrottler destructor flushes a pending emission on the calling thread" )
{
    EmissionRecorder recorder;
    {
        klogg::TrailingThrottler throttler( 5s, [&recorder] { recorder.record(); } );
        throttler.trigger();
    }

    REQUIRE( recorder.count() == 1 );
    const auto thread = recorder.firstThread();
    REQUIRE( thread.has_value() );
    REQUIRE( *thread == std::this_thread::get_id() );
}

TEST_CASE( "TrailingThrottler destructor does not re-emit after the window fired" )
{
    EmissionRecorder recorder;
    const auto start = std::chrono::steady_clock::now();
    {
        klogg::TrailingThrottler throttler( 100ms, [&recorder] { recorder.record(); } );
        throttler.trigger();
        REQUIRE( waitForCount( recorder, 1, start + 500ms ) );
    }

    REQUIRE( recorder.count() == 1 );
}

TEST_CASE( "TrailingThrottler runs the emission on the internal timer thread" )
{
    EmissionRecorder recorder;
    const auto start = std::chrono::steady_clock::now();
    klogg::TrailingThrottler throttler( 100ms, [&recorder] { recorder.record(); } );
    throttler.trigger();

    REQUIRE( waitForCount( recorder, 1, start + 500ms ) );
    const auto thread = recorder.firstThread();
    REQUIRE( thread.has_value() );
    REQUIRE( *thread != std::this_thread::get_id() );
}

TEST_CASE( "TrailingThrottler survives a callback that throws" )
{
    EmissionRecorder recorder;
    klogg::TrailingThrottler throttler( 100ms, [&recorder] {
        recorder.record();
        throw std::runtime_error( "throttler callback failure" );
    } );

    const auto start = std::chrono::steady_clock::now();
    throttler.trigger();
    // An exception escaping the timer loop terminates the process.
    REQUIRE( waitForCount( recorder, 1, start + 500ms ) );

    // The timer thread survived the throw: the next burst still emits.
    throttler.trigger();
    REQUIRE( waitForCount( recorder, 2, start + 1500ms ) );
    REQUIRE( recorder.count() == 2 );
}

TEST_CASE( "TrailingThrottler destructor flush survives a callback that throws" )
{
    EmissionRecorder recorder;
    bool destructorReturned = false;
    {
        klogg::TrailingThrottler throttler( 5s, [&recorder] {
            recorder.record();
            throw std::runtime_error( "throttler flush failure" );
        } );
        throttler.trigger();
    }
    // The destructor is implicitly noexcept; an escaping exception would have
    // terminated the process before this line.
    destructorReturned = true;

    REQUIRE( destructorReturned );
    REQUIRE( recorder.count() == 1 );
    const auto thread = recorder.firstThread();
    REQUIRE( thread.has_value() );
    REQUIRE( *thread == std::this_thread::get_id() );
}
