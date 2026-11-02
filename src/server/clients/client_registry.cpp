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

#include "client_registry.h"

#include <chrono>
#include <utility>

namespace klogg::server {

namespace {

constexpr const char* kHumanRole = "human";
constexpr const char* kAiRole = "ai";

} // namespace

std::vector<std::string> ClientRegistry::capabilitiesFor( const std::string& role )
{
    // Config (highlighters, filters, persisted user data) belongs to the GUI
    // owner; an AI client observes and annotates, it does not rewrite it.
    if ( role == kHumanRole ) {
        return { "read", "search", "mark", "view", "config", "recorder" };
    }
    return { "read", "search", "mark", "view" };
}

std::int64_t ClientRegistry::nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch() )
        .count();
}

std::shared_ptr<ClientInfo> ClientRegistry::create( std::string name, std::string role )
{
    if ( role != kHumanRole && role != kAiRole ) {
        role = kAiRole;
    }

    auto info = std::make_shared<ClientInfo>();
    {
        std::lock_guard lock( mutex_ );
        info->id = "c" + std::to_string( nextId_++ );
        info->name = name.empty() ? "anonymous" : std::move( name );
        info->role = std::move( role );
        info->capabilities = capabilitiesFor( info->role );
        info->lastActiveMs = nowMs();
        clients_.emplace( info->id, info );
    }
    return info;
}

std::shared_ptr<ClientInfo> ClientRegistry::find( const std::string& id ) const
{
    std::lock_guard lock( mutex_ );
    const auto it = clients_.find( id );
    return it == clients_.end() ? nullptr : it->second;
}

std::shared_ptr<ClientInfo> ClientRegistry::getOrCreate( const std::string& id, std::string role )
{
    if ( !id.empty() ) {
        std::lock_guard lock( mutex_ );
        const auto it = clients_.find( id );
        if ( it != clients_.end() ) {
            it->second->lastActiveMs = nowMs();
            return it->second;
        }
    }
    return create( "anonymous", std::move( role ) );
}

std::vector<ClientInfo> ClientRegistry::list() const
{
    std::lock_guard lock( mutex_ );
    std::vector<ClientInfo> result;
    result.reserve( clients_.size() );
    for ( const auto& [ id, info ] : clients_ ) {
        result.push_back( *info );
    }
    return result;
}

ClientResources ClientRegistry::remove( const std::string& id )
{
    std::lock_guard lock( mutex_ );
    const auto it = clients_.find( id );
    if ( it == clients_.end() ) {
        return {};
    }

    ClientResources resources;
    resources.searches.assign( it->second->searches.begin(), it->second->searches.end() );
    for ( const auto& [ fileId, references ] : it->second->files ) {
        for ( int i = 0; i < references; ++i ) {
            resources.files.push_back( fileId );
        }
    }
    clients_.erase( it );
    return resources;
}

void ClientRegistry::addSearch( const std::string& id, api::SearchId searchId )
{
    std::lock_guard lock( mutex_ );
    const auto it = clients_.find( id );
    if ( it != clients_.end() ) {
        it->second->searches.insert( searchId );
        it->second->lastActiveMs = nowMs();
    }
}

void ClientRegistry::removeSearch( const std::string& id, api::SearchId searchId )
{
    std::lock_guard lock( mutex_ );
    const auto it = clients_.find( id );
    if ( it != clients_.end() ) {
        it->second->searches.erase( searchId );
    }
}

bool ClientRegistry::ownsSearch( const std::string& id, api::SearchId searchId ) const
{
    std::lock_guard lock( mutex_ );
    const auto it = clients_.find( id );
    return it != clients_.end() && it->second->searches.count( searchId ) != 0;
}

void ClientRegistry::addFile( const std::string& id, api::FileId fileId )
{
    std::lock_guard lock( mutex_ );
    const auto it = clients_.find( id );
    if ( it != clients_.end() ) {
        ++it->second->files[ fileId ];
        it->second->lastActiveMs = nowMs();
    }
}

void ClientRegistry::removeFile( const std::string& id, api::FileId fileId )
{
    std::lock_guard lock( mutex_ );
    const auto it = clients_.find( id );
    if ( it == clients_.end() ) {
        return;
    }
    const auto file = it->second->files.find( fileId );
    if ( file == it->second->files.end() ) {
        return;
    }
    if ( --file->second <= 0 ) {
        it->second->files.erase( file );
    }
}

bool ClientRegistry::ownsFile( const std::string& id, api::FileId fileId ) const
{
    std::lock_guard lock( mutex_ );
    const auto it = clients_.find( id );
    if ( it == clients_.end() ) {
        return false;
    }
    const auto file = it->second->files.find( fileId );
    return file != it->second->files.end() && file->second > 0;
}

void ClientRegistry::touch( const std::string& id )
{
    std::lock_guard lock( mutex_ );
    const auto it = clients_.find( id );
    if ( it != clients_.end() ) {
        it->second->lastActiveMs = nowMs();
    }
}

} // namespace klogg::server
