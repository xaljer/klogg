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

#include <string>

#include <nlohmann/json.hpp>

#include "klogg/api.h"
#include "klogg/server/server.h"

#include "client_registry.h"
#include "recorder_config.h"
#include "recorder_manager.h"

namespace klogg::server {

// One JSON response of the RPC surface. `clientId` is the effective client of
// the request (new for session.connect), so the transport can echo it in the
// X-Klogg-Client-Id header.
struct RpcResponse {
    int status = 200;
    nlohmann::json body;
    std::string clientId;
};

// Maps POST /api/<method> to Session calls. The dispatcher owns no transport:
// it is testable without a socket and reusable by any future C ABI / WS shell.
class RpcDispatcher {
public:
    RpcDispatcher( api::Session& session, ClientRegistry& registry, const ServerOptions& options );

    RpcResponse dispatch( const std::string& method, const std::string& clientId,
                          const nlohmann::json& params );

    // Called by the transport when the owning SSE client disappears or the
    // server shuts down; recorder children must not outlive the session.
    void stopClient( const std::string& clientId );

private:
    RpcResponse ok( nlohmann::json value ) const;
    RpcResponse fail( const api::Error& error ) const;
    RpcResponse fail( int status, std::string code, std::string message ) const;

    bool pathAllowed( const std::string& path ) const;
    bool canUseRecorder( const std::string& clientId ) const;

    api::Session& session_;
    ClientRegistry& registry_;
    const ServerOptions& options_;
    RecorderConfig recorderConfig_;
    RecorderManager recorder_;
};

} // namespace klogg::server
