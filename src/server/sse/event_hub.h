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

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "klogg/api.h"

namespace klogg::server {

// One event queued for an SSE connection. The sequence number is assigned by
// the writer, not here: it is per connection (the SSE id field).
struct HubEvent {
    std::string type;
    std::string originClientId;
    nlohmann::json payload;
};

struct ClientStream {
    std::string id;
    std::deque<HubEvent> queue;
    std::uint64_t seq = 0;
    bool helloWritten = false;
    bool closed = false;
};

// Fans the Session's event subscription out to the connected SSE clients. The
// subscription itself lives as long as the Session; the shared State lets a
// late event after Server destruction observe `stopped` and do nothing.
class EventHub {
public:
    EventHub( api::Session& session, std::size_t maxEventsPerClient );
    ~EventHub();

    EventHub( const EventHub& ) = delete;
    EventHub& operator=( const EventHub& ) = delete;

    void start();
    void stop();

    std::shared_ptr<ClientStream> addClient( const std::string& id );
    void removeClient( const std::string& id );

    // False when a reconnect replaced this stream: the old provider must exit
    // without releasing the client's new resources.
    bool isCurrent( const std::string& id, const std::shared_ptr<ClientStream>& client ) const;

    // Blocks up to timeoutMs for the next event of this client. nullopt means
    // timeout or a closed stream; the caller sends a heartbeat on timeout.
    std::optional<HubEvent> nextEvent( const std::shared_ptr<ClientStream>& client, int timeoutMs );

    // Server-originated event (presence, view sharing, ...).
    void broadcast( const std::string& type, const nlohmann::json& payload,
                    const std::string& originClientId );

private:
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        std::map<std::string, std::shared_ptr<ClientStream>> clients;
        bool stopped = false;
    };

    static void publish( const std::shared_ptr<State>& state, const HubEvent& event,
                         std::size_t maxQueue );

    api::Session& session_;
    std::shared_ptr<State> state_ = std::make_shared<State>();
    std::size_t maxEventsPerClient_;
    bool subscribed_ = false;
};

} // namespace klogg::server
