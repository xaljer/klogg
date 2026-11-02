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

#include "klogg/server/server.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#include "httplib.h"

#include "klogg_version.h"

#include "client_registry.h"
#include "event_hub.h"
#include "json_convert.h"
#include "rpc_dispatcher.h"

namespace klogg::server {

namespace {

constexpr const char* kStatusHtml = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>klogg server</title>
<style>body{font:14px system-ui,sans-serif;background:#1e1e2e;color:#cdd6f4;padding:2rem}
code{background:#313244;padding:2px 6px;border-radius:4px}</style></head>
<body><h1>klogg server</h1>
<p>The HTTP API is running. See <code>docs/architecture/web-api.md</code>.</p>
<p>Probe: <code>POST /api/system.info</code></p></body></html>)HTML";

bool isLoopbackAddress( const std::string& address )
{
    return address == "127.0.0.1" || address == "localhost" || address == "::1"
           || address == "[::1]";
}

bool isAllowedOrigin( const std::string& origin, bool anyOrigin )
{
    if ( origin.empty() ) {
        return false;
    }
    if ( anyOrigin ) {
        return true;
    }
    const auto isLocal
        = [ & ]( const std::string& prefix ) { return origin.rfind( prefix, 0 ) == 0; };
    return isLocal( "http://localhost:" ) || isLocal( "http://127.0.0.1:" )
           || isLocal( "http://[::1]:" ) || isLocal( "https://localhost:" )
           || isLocal( "https://127.0.0.1:" ) || isLocal( "https://[::1]:" )
           || origin == "http://localhost" || origin == "http://127.0.0.1";
}

// Log payloads are arbitrary bytes. JSON strings must be valid UTF-8, so
// invalid sequences are replaced rather than throwing out of the response.
std::string dumpJson( const nlohmann::json& value )
{
    return value.dump( -1, ' ', false, nlohmann::json::error_handler_t::replace );
}

} // namespace

struct Server::Impl {
    Impl( api::Session& session_, ServerOptions options_ )
        : session( session_ )
        , options( std::move( options_ ) )
        , hub( session, options.maxEventsPerClient )
        , dispatcher( session, registry, options )
    {
    }

    api::Result<void> start();
    void stop();

    void dropClient( const std::string& id );
    bool writeSseEvent( httplib::DataSink& sink, ClientStream& stream, const HubEvent& event );
    void installRoutes();

    api::Session& session;
    ServerOptions options;
    ClientRegistry registry;
    EventHub hub;
    RpcDispatcher dispatcher;
    httplib::Server http;
    std::thread thread;
    std::atomic<bool> started{ false };
    int boundPort = 0;
};

void Server::Impl::dropClient( const std::string& id )
{
    dispatcher.stopClient( id );
    const auto resources = registry.remove( id );
    for ( const auto search : resources.searches ) {
        session.cancelSearch( search );
    }
    for ( const auto file : resources.files ) {
        session.closeFile( file );
    }
    hub.removeClient( id );
}

bool Server::Impl::writeSseEvent( httplib::DataSink& sink, ClientStream& stream,
                                  const HubEvent& event )
{
    const nlohmann::json envelope = {
        { "event", event.type },
        { "seq", stream.seq },
        { "originClientId", event.originClientId },
        { "payload", event.payload },
    };
    const auto data = "id: " + std::to_string( stream.seq ) + "\nevent: " + event.type
                      + "\ndata: " + dumpJson( envelope ) + "\n\n";
    ++stream.seq;
    return sink.write( data.data(), data.size() );
}

void Server::Impl::installRoutes()
{
    http.set_pre_routing_handler( [ this ]( const httplib::Request& request,
                                            httplib::Response& response ) {
        const auto origin = request.get_header_value( "Origin" );
        if ( isAllowedOrigin( origin, options.allowAnyOrigin ) ) {
            response.set_header( "Access-Control-Allow-Origin", origin );
            response.set_header( "Vary", "Origin" );
            response.set_header( "Access-Control-Allow-Methods", "GET, POST, OPTIONS" );
            response.set_header( "Access-Control-Allow-Headers",
                                 "Content-Type, Authorization, X-Klogg-Client-Id" );
            response.set_header( "Access-Control-Expose-Headers", "X-Klogg-Client-Id" );
            response.set_header( "Access-Control-Max-Age", "600" );
        }

        if ( request.method == "OPTIONS" ) {
            response.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }

        if ( !options.token.empty() && request.path.rfind( "/api/", 0 ) == 0 ) {
            const auto authorization = request.get_header_value( "Authorization" );
            const auto queryToken = request.get_param_value( "token" );
            const auto expected = "Bearer " + options.token;
            if ( authorization != expected
                 && ( queryToken.empty() || queryToken != options.token ) ) {
                response.status = 401;
                response.set_content(
                    R"({"ok":false,"error":{"code":"Unauthorized","message":"missing or invalid token"}})",
                    "application/json" );
                return httplib::Server::HandlerResponse::Handled;
            }
        }

        return httplib::Server::HandlerResponse::Unhandled;
    } );

    http.Post( R"(/api/([A-Za-z0-9_.]+))", [ this ]( const httplib::Request& request,
                                                     httplib::Response& response ) {
        const auto method = request.matches[ 1 ].str();
        std::string clientId = request.get_header_value( "X-Klogg-Client-Id" );

        if ( method != "session.connect" ) {
            if ( clientId.empty() || !registry.find( clientId ) ) {
                clientId = registry.create( "anonymous", "human" )->id;
            }
            else {
                registry.touch( clientId );
            }
        }

        nlohmann::json params = nlohmann::json::object();
        if ( !request.body.empty() ) {
            params = nlohmann::json::parse( request.body, nullptr, false );
            if ( params.is_discarded() ) {
                response.status = 400;
                response.set_content(
                    R"({"ok":false,"error":{"code":"InvalidRequest","message":"body is not valid JSON"}})",
                    "application/json" );
                return;
            }
        }

        auto result = dispatcher.dispatch( method, clientId, params );
        if ( !result.clientId.empty() ) {
            clientId = result.clientId;
        }
        if ( !clientId.empty() ) {
            response.set_header( "X-Klogg-Client-Id", clientId );
        }
        response.status = result.status;
        response.set_content( dumpJson( result.body ), "application/json" );
    } );

    http.Get(
        "/api/events", [ this ]( const httplib::Request& request, httplib::Response& response ) {
            const auto requested = request.get_param_value( "clientId" );
            auto info = requested.empty() ? nullptr : registry.find( requested );
            if ( !info ) {
                info = registry.create( "sse", "human" );
            }

            auto stream = hub.addClient( info->id );
            const auto cleaned = std::make_shared<bool>( false );
            // EventSource reconnects carry Last-Event-ID; the MVP does not
            // replay the gap, it asks the client to refetch the full state.
            const bool reconnected = request.has_header( "Last-Event-ID" );

            response.set_header( "Cache-Control", "no-cache" );
            response.set_header( "X-Accel-Buffering", "no" );
            response.set_header( "Connection", "keep-alive" );
            response.set_chunked_content_provider(
                "text/event-stream",
                [ this, info, stream, cleaned, reconnected ]( std::size_t,
                                                              httplib::DataSink& sink ) -> bool {
                    const auto cleanup = [ this, info, stream, cleaned ] {
                        if ( *cleaned ) {
                            return;
                        }
                        *cleaned = true;
                        if ( !hub.isCurrent( info->id, stream ) ) {
                            return;
                        }
                        dropClient( info->id );
                    };

                    if ( !stream->helloWritten ) {
                        stream->helloWritten = true;
                        if ( !writeSseEvent( sink, *stream,
                                             HubEvent{ "client.hello",
                                                       "server",
                                                       { { "clientId", info->id },
                                                         { "apiVersion", kloggApiVersion() },
                                                         { "serverVersion", session.version() },
                                                         { "capabilities", info->capabilities },
                                                         { "role", info->role } } } ) ) {
                            cleanup();
                            return false;
                        }
                        if ( reconnected
                             && !writeSseEvent( sink, *stream,
                                                HubEvent{ "stream.reset",
                                                          "server",
                                                          { { "reason", "reconnect" } } } ) ) {
                            cleanup();
                            return false;
                        }
                    }

                    auto event = hub.nextEvent( stream, 15000 );
                    if ( event ) {
                        if ( !writeSseEvent( sink, *stream, *event ) ) {
                            cleanup();
                            return false;
                        }
                        return true;
                    }

                    if ( stream->closed || !sink.is_writable() ) {
                        cleanup();
                        return false;
                    }

                    static constexpr char kPing[] = ": ping\n\n";
                    if ( !sink.write( kPing, sizeof( kPing ) - 1 ) ) {
                        cleanup();
                        return false;
                    }
                    return true;
                },
                [ cleaned ]( bool ) { *cleaned = true; } );
        } );

    http.Get( "/health", []( const httplib::Request&, httplib::Response& response ) {
        response.set_content( R"({"ok":true})", "application/json" );
    } );

    if ( options.webRoot.empty() ) {
        http.Get( "/", []( const httplib::Request&, httplib::Response& response ) {
            response.set_content( kStatusHtml, "text/html; charset=utf-8" );
        } );
    }
    else {
        // The UI entry point changes on every build (it names hashed assets),
        // so browsers must revalidate it instead of applying heuristic caching.
        (void)http.set_mount_point( "/", options.webRoot.string(),
                                    httplib::Headers{ { "Cache-Control", "no-cache" } } );
    }
}

api::Result<void> Server::Impl::start()
{
    if ( !isLoopbackAddress( options.listenAddress ) && options.token.empty() ) {
        return tl::make_unexpected( api::Error{
            api::ErrorCode::InternalError, "a non-loopback listen address requires a token" } );
    }
    if ( options.port < 0 || options.port > 65535 ) {
        return tl::make_unexpected( api::Error{ api::ErrorCode::InternalError, "invalid port" } );
    }
    if ( !options.webRoot.empty() && !std::filesystem::is_directory( options.webRoot ) ) {
        return tl::make_unexpected(
            api::Error{ api::ErrorCode::InternalError, "webRoot is not a directory" } );
    }

    http.set_payload_max_length( options.maxBodyBytes );
    http.set_keep_alive_max_count( 100 );
    http.set_keep_alive_timeout( 30 );
    http.set_read_timeout( 30, 0 );
    http.set_write_timeout( 30, 0 );
    http.new_task_queue = [ count = options.threadCount ] {
        return new httplib::ThreadPool( static_cast<std::size_t>( std::max( count, 4 ) ) );
    };

    installRoutes();

    if ( options.port == 0 ) {
        boundPort = http.bind_to_any_port( options.listenAddress );
        if ( boundPort <= 0 ) {
            return tl::make_unexpected(
                api::Error{ api::ErrorCode::InternalError, "cannot bind the HTTP port" } );
        }
    }
    else {
        if ( !http.bind_to_port( options.listenAddress, options.port ) ) {
            return tl::make_unexpected(
                api::Error{ api::ErrorCode::InternalError, "cannot bind the HTTP port" } );
        }
        boundPort = options.port;
    }

    hub.start();
    thread = std::thread( [ this ] { http.listen_after_bind(); } );

    for ( int i = 0; i < 500 && !http.is_running(); ++i ) {
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    if ( !http.is_running() ) {
        hub.stop();
        http.stop();
        if ( thread.joinable() ) {
            thread.join();
        }
        return tl::make_unexpected(
            api::Error{ api::ErrorCode::InternalError, "HTTP server did not start" } );
    }

    started = true;
    return {};
}

void Server::Impl::stop()
{
    hub.stop();
    http.stop();
    if ( thread.joinable() ) {
        thread.join();
    }
    if ( started.exchange( false ) ) {
        for ( const auto& info : registry.list() ) {
            dispatcher.stopClient( info.id );
            const auto resources = registry.remove( info.id );
            for ( const auto search : resources.searches ) {
                session.cancelSearch( search );
            }
            for ( const auto file : resources.files ) {
                session.closeFile( file );
            }
        }
    }
}

Server::Server( api::Session& session, ServerOptions options )
    : impl_( std::make_unique<Impl>( session, std::move( options ) ) )
{
}

Server::~Server()
{
    stop();
}

api::Result<std::unique_ptr<Server>> Server::create( api::Session& session, ServerOptions options )
{
    if ( options.maxBodyBytes == 0 ) {
        return tl::make_unexpected(
            api::Error{ api::ErrorCode::InternalError, "maxBodyBytes must be positive" } );
    }
    return std::unique_ptr<Server>( new Server( session, std::move( options ) ) );
}

api::Result<void> Server::start()
{
    return impl_->start();
}

void Server::stop()
{
    impl_->stop();
}

int Server::port() const
{
    return impl_->boundPort;
}

std::string Server::url() const
{
    return "http://" + impl_->options.listenAddress + ":" + std::to_string( impl_->boundPort );
}

} // namespace klogg::server
