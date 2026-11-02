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

#pragma once

// Thread-level tracing for the api module, used to locate marshal waits,
// lock contention and copy hotspots while moving the engine off Qt threads.
// Built only with -DKLOGG_USE_PERFETTO=ON; when off, every KLOGG_TRACE_*
// macro expands to nothing (the preprocessor drops the arguments, so not
// even the strings reach the binary) and the helpers below are no-ops.

#ifdef KLOGG_USE_PERFETTO

#include <perfetto.h>

// "api" covers the marshal boundary on caller threads, "api.read" the read
// channel executing directly on the caller thread (C4-5), "engine" the
// worker-side execution, "ui" the GUI-side handlers and paint paths (see
// src/ui/include/uitracing.h). Kept in a dedicated namespace so other modules
// can later define their own sets (only one global set is allowed per
// program).
PERFETTO_DEFINE_CATEGORIES_IN_NAMESPACE( klogg_tracing, perfetto::Category( "api" ),
                                         perfetto::Category( "api.read" ),
                                         perfetto::Category( "engine" ),
                                         perfetto::Category( "ui" ) );
PERFETTO_USE_CATEGORIES_FROM_NAMESPACE( klogg_tracing );

namespace klogg::api::tracing {

// Starts an in-process tracing session writing to the file named by the
// KLOGG_PERFETTO_TRACE environment variable; does nothing when the variable
// is unset. Idempotent, safe to call from every Session::create().
void initializeFromEnvironment();

// Stops the session and flushes buffered events to the trace file.
void flushAndStop();

} // namespace klogg::api::tracing

#define KLOGG_TRACE_SCOPE( category, name ) TRACE_EVENT( category, name )
#define KLOGG_TRACE_COUNTER( category, track, value ) TRACE_COUNTER( category, track, value )

#else // KLOGG_USE_PERFETTO

namespace klogg::api::tracing {

inline void initializeFromEnvironment()
{
}

inline void flushAndStop()
{
}

} // namespace klogg::api::tracing

#define KLOGG_TRACE_SCOPE( category, name ) ( (void)0 )
#define KLOGG_TRACE_COUNTER( category, track, value ) ( (void)0 )

#endif // KLOGG_USE_PERFETTO
