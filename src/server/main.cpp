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

// Temporary standalone host of the zero-Qt HTTP server. The product's CLI
// (klogg_cli) will absorb this entry point as a `serve` subcommand; the server
// core itself is already independent of this file.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "klogg/server/server.h"

#include "klogg_version.h"
#include "userdata.h"

namespace {

std::atomic<bool> stopRequested{ false };

void onSignal( int )
{
    stopRequested = true;
}

void printUsage()
{
    std::cout << "Usage: klogg_server [options]\n"
              << "\n"
              << "  --port <n>          listen port (0 = ephemeral, default 8080)\n"
              << "  --listen <addr>     listen address (default 127.0.0.1)\n"
              << "  --token <token>     bearer token for /api (or KLOGG_SERVER_TOKEN)\n"
              << "  --root <dir>        only allow opening files under this directory\n"
              << "  --web-root <dir>    serve the built web UI from this directory\n"
              << "  --cors              allow any CORS origin (development)\n"
              << "  --max-body <bytes>  request body limit (default 1048576)\n"
              << "  --version           print the version and exit\n"
              << "  -h, --help          this help\n"
              << "\n"
              << "A non-loopback --listen address requires --token.\n";
}

bool parsePort( const std::string& text, int& port )
{
    try {
        std::size_t consumed = 0;
        const auto value = std::stoi( text, &consumed );
        if ( consumed != text.size() || value < 0 || value > 65535 ) {
            return false;
        }
        port = value;
        return true;
    } catch ( const std::exception& ) {
        return false;
    }
}

bool parseSize( const std::string& text, std::size_t& value )
{
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull( text, &consumed );
        if ( consumed != text.size() || parsed == 0 ) {
            return false;
        }
        value = static_cast<std::size_t>( parsed );
        return true;
    } catch ( const std::exception& ) {
        return false;
    }
}

std::string takeValue( int& index, int argc, char* argv[], const std::string& option )
{
    if ( index + 1 >= argc ) {
        std::cerr << option << " requires a value\n";
        std::exit( 2 );
    }
    return argv[ ++index ];
}

} // namespace

int main( int argc, char* argv[] )
{
    klogg::server::ServerOptions options;

    if ( const auto* token = std::getenv( "KLOGG_SERVER_TOKEN" ); token != nullptr ) {
        options.token = token;
    }

    for ( int i = 1; i < argc; ++i ) {
        const std::string argument = argv[ i ];
        if ( argument == "-h" || argument == "--help" ) {
            printUsage();
            return 0;
        }
        if ( argument == "--version" ) {
            std::cout << "klogg_server " << kloggAppVersion() << "\n";
            return 0;
        }
        if ( argument == "--port" ) {
            const auto value = takeValue( i, argc, argv, argument );
            if ( !parsePort( value, options.port ) ) {
                std::cerr << "invalid port: " << value << "\n";
                return 2;
            }
            continue;
        }
        if ( argument.rfind( "--port=", 0 ) == 0 ) {
            const auto value = argument.substr( 7 );
            if ( !parsePort( value, options.port ) ) {
                std::cerr << "invalid port: " << value << "\n";
                return 2;
            }
            continue;
        }
        if ( argument == "--listen" ) {
            options.listenAddress = takeValue( i, argc, argv, argument );
            continue;
        }
        if ( argument.rfind( "--listen=", 0 ) == 0 ) {
            options.listenAddress = argument.substr( 9 );
            continue;
        }
        if ( argument == "--token" ) {
            options.token = takeValue( i, argc, argv, argument );
            continue;
        }
        if ( argument.rfind( "--token=", 0 ) == 0 ) {
            options.token = argument.substr( 8 );
            continue;
        }
        if ( argument == "--root" ) {
            options.root = takeValue( i, argc, argv, argument );
            continue;
        }
        if ( argument.rfind( "--root=", 0 ) == 0 ) {
            options.root = argument.substr( 7 );
            continue;
        }
        if ( argument == "--web-root" ) {
            options.webRoot = takeValue( i, argc, argv, argument );
            continue;
        }
        if ( argument.rfind( "--web-root=", 0 ) == 0 ) {
            options.webRoot = argument.substr( 11 );
            continue;
        }
        if ( argument == "--cors" ) {
            options.allowAnyOrigin = true;
            continue;
        }
        if ( argument == "--max-body" ) {
            const auto value = takeValue( i, argc, argv, argument );
            if ( !parseSize( value, options.maxBodyBytes ) ) {
                std::cerr << "invalid max body size: " << value << "\n";
                return 2;
            }
            continue;
        }
        if ( argument.rfind( "--max-body=", 0 ) == 0 ) {
            const auto value = argument.substr( 11 );
            if ( !parseSize( value, options.maxBodyBytes ) ) {
                std::cerr << "invalid max body size: " << value << "\n";
                return 2;
            }
            continue;
        }

        std::cerr << "unknown argument: " << argument << "\n";
        printUsage();
        return 2;
    }

    std::signal( SIGINT, onSignal );
#ifdef SIGTERM
    std::signal( SIGTERM, onSignal );
#endif

    platform::userdata::initialize( false );

    auto session = klogg::api::Session::create();
    if ( !session ) {
        std::cerr << "cannot create session: " << session.error().message << "\n";
        return 1;
    }

    auto server = klogg::server::Server::create( **session, options );
    if ( !server ) {
        std::cerr << "cannot create server: " << server.error().message << "\n";
        return 1;
    }

    const auto started = ( *server )->start();
    if ( !started ) {
        std::cerr << "cannot start server: " << started.error().message << "\n";
        return 1;
    }

    std::cout << "klogg_server " << kloggAppVersion() << " listening on " << ( *server )->url()
              << std::endl;
    if ( !options.token.empty() ) {
        std::cout << "bearer token required for /api" << std::endl;
    }

    while ( !stopRequested ) {
        std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    }

    ( *server )->stop();
    return 0;
}
