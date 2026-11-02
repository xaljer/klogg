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

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

#include "klogg/api.h"

namespace klogg::server {

// Transport and policy settings of one HTTP endpoint. The server never owns
// the Session: the CLI host creates one, and the GUI host passes the session it
// already shares with its windows.
struct ServerOptions {
    // Loopback by default: a local tool must not become a remote service by
    // accident. A non-loopback address requires a token.
    std::string listenAddress = "127.0.0.1";
    int port = 8080;

    // Bearer token for every /api request (SSE may pass ?token= because
    // EventSource cannot set headers). Empty = no authentication, only allowed
    // on a loopback address.
    std::string token;

    // Optional allowlist for file.open. Empty = any path the process can read.
    std::filesystem::path root;

    // Optional directory with the built web UI. Empty = the built-in status
    // page only.
    std::filesystem::path webRoot;

    // Development escape hatch: allow any CORS origin. Off by default; the
    // built UI is same-origin and Vite dev uses localhost.
    bool allowAnyOrigin = false;

    std::size_t maxBodyBytes = 1 << 20;
    std::size_t maxLinesPerRequest = 10000;
    std::size_t maxEventsPerClient = 1024;
    int threadCount = 32;
};

// One HTTP endpoint over one Session. Start() runs the accept loop on a
// background thread; Stop() is idempotent and joins it. The Session must
// outlive the Server.
class Server {
public:
    static api::Result<std::unique_ptr<Server>> create( api::Session& session,
                                                        ServerOptions options = {} );
    ~Server();

    Server( const Server& ) = delete;
    Server& operator=( const Server& ) = delete;

    // Binds, starts the accept loop and waits until it is running. Port 0
    // binds an ephemeral port; port() reports the actual one.
    api::Result<void> start();
    void stop();

    int port() const;
    std::string url() const;

private:
    Server( api::Session& session, ServerOptions options );

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace klogg::server
