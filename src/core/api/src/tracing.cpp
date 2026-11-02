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

#include "tracing.h"

#ifdef KLOGG_USE_PERFETTO

#if defined( _WIN32 )
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <cstdlib>
#include <mutex>

#include "log.h"

PERFETTO_TRACK_EVENT_STATIC_STORAGE_IN_NAMESPACE( klogg_tracing );

namespace klogg::api::tracing {
namespace {

std::once_flag initOnce;

// Never deleted: flushAndStop() runs from atexit, where part of the static
// teardown may have happened already, so the session is only stopped, not
// destroyed, and the process exit reclaims it.
perfetto::TracingSession* session = nullptr;
int traceFd = -1;

int openTraceFile( const char* path )
{
#if defined( _WIN32 )
    return ::_open( path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY );
#else
    return ::open( path, O_WRONLY | O_CREAT | O_TRUNC, 0644 );
#endif
}

void closeTraceFile( int fd )
{
#if defined( _WIN32 )
    ::_close( fd );
#else
    ::close( fd );
#endif
}

void startSession()
{
    const char* tracePath = std::getenv( "KLOGG_PERFETTO_TRACE" );
    if ( !tracePath || !*tracePath ) {
        return;
    }

    const int fd = openTraceFile( tracePath );
    if ( fd < 0 ) {
        LOG_WARNING << "perfetto: failed to open trace file" << tracePath;
        return;
    }

    perfetto::TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend;
    perfetto::Tracing::Initialize( args );
    klogg_tracing::TrackEvent::Register();

    perfetto::TraceConfig config;
    config.add_buffers()->set_size_kb( 4096 );
    config.add_data_sources()->mutable_config()->set_name( "track_event" );

    traceFd = fd;
    session = perfetto::Tracing::NewTrace( perfetto::kInProcessBackend ).release();
    session->Setup( config, traceFd );
    session->StartBlocking();

    // Flushing on Session destruction would truncate the trace at the first
    // destroyed session (tests and the UI create several per process), so the
    // one flush happens at process exit. Perfetto's own statics are in place
    // long before this registration, hence atexit runs ahead of their
    // teardown.
    std::atexit( &flushAndStop );
}

} // namespace

void initializeFromEnvironment()
{
    std::call_once( initOnce, &startSession );
}

void flushAndStop()
{
    if ( !session ) {
        return;
    }
    session->FlushBlocking( 5000 );
    session->StopBlocking();
    session = nullptr;
    closeTraceFile( traceFd );
    traceFd = -1;
}

} // namespace klogg::api::tracing

#endif // KLOGG_USE_PERFETTO
