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

#include "event_hub.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace klogg::server {

EventHub::EventHub( api::Session& session, std::size_t maxEventsPerClient )
    : session_( session )
    , maxEventsPerClient_( std::max<std::size_t>( maxEventsPerClient, 16 ) )
{
}

EventHub::~EventHub()
{
    stop();
}

void EventHub::start()
{
    if ( subscribed_ ) {
        return;
    }
    subscribed_ = true;

    auto state = state_;
    const auto maxEvents = maxEventsPerClient_;
    session_.subscribe( [ state, maxEvents ]( const std::string& type, const std::string& text ) {
        nlohmann::json payload;
        try {
            payload = nlohmann::json::parse( text );
        } catch ( const std::exception& ) {
            payload = text;
        }
        publish( state, HubEvent{ type, "server", std::move( payload ) }, maxEvents );
    } );
}

void EventHub::stop()
{
    std::lock_guard lock( state_->mutex );
    state_->stopped = true;
    for ( auto& [ id, client ] : state_->clients ) {
        client->closed = true;
    }
    state_->clients.clear();
    state_->cv.notify_all();
}

std::shared_ptr<ClientStream> EventHub::addClient( const std::string& id )
{
    auto client = std::make_shared<ClientStream>();
    client->id = id;

    std::lock_guard lock( state_->mutex );
    if ( state_->stopped ) {
        client->closed = true;
        return client;
    }

    const auto existing = state_->clients.find( id );
    if ( existing != state_->clients.end() ) {
        existing->second->closed = true;
    }
    state_->clients[ id ] = client;
    state_->cv.notify_all();
    return client;
}

void EventHub::removeClient( const std::string& id )
{
    std::lock_guard lock( state_->mutex );
    const auto it = state_->clients.find( id );
    if ( it != state_->clients.end() ) {
        it->second->closed = true;
        state_->clients.erase( it );
    }
    state_->cv.notify_all();
}

bool EventHub::isCurrent( const std::string& id, const std::shared_ptr<ClientStream>& client ) const
{
    std::lock_guard lock( state_->mutex );
    const auto it = state_->clients.find( id );
    return it != state_->clients.end() && it->second == client && !client->closed;
}

std::optional<HubEvent> EventHub::nextEvent( const std::shared_ptr<ClientStream>& client,
                                             int timeoutMs )
{
    std::unique_lock lock( state_->mutex );
    state_->cv.wait_for( lock, std::chrono::milliseconds( timeoutMs ), [ & ] {
        return state_->stopped || client->closed || !client->queue.empty();
    } );

    if ( state_->stopped || client->closed || client->queue.empty() ) {
        return std::nullopt;
    }

    auto event = std::move( client->queue.front() );
    client->queue.pop_front();
    return event;
}

void EventHub::broadcast( const std::string& type, const nlohmann::json& payload,
                          const std::string& originClientId )
{
    publish( state_, HubEvent{ type, originClientId, payload }, maxEventsPerClient_ );
}

void EventHub::publish( const std::shared_ptr<State>& state, const HubEvent& event,
                        std::size_t maxQueue )
{
    std::lock_guard lock( state->mutex );
    if ( state->stopped ) {
        return;
    }

    for ( auto& [ id, client ] : state->clients ) {
        if ( client->closed ) {
            continue;
        }
        if ( client->queue.size() >= maxQueue ) {
            client->queue.clear();
            client->queue.push_back(
                HubEvent{ "stream.reset", "server", { { "reason", "overflow" } } } );
            continue;
        }
        client->queue.push_back( event );
    }
    state->cv.notify_all();
}

} // namespace klogg::server
