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

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "klogg/api.h"

namespace klogg::server {

// One remote participant. HTTP requests carry its id, and a search created by
// that client is tracked here so a dropped SSE connection can cancel exactly
// its own searches (the Core contract is "creator cancels").
struct ClientInfo {
    std::string id;
    std::string name;
    std::string role; // human | ai
    std::vector<std::string> capabilities;
    std::set<api::SearchId> searches;
    // Open-file references this client owns. The engine counts one reference
    // per successful open; closing a file this client never opened would drop
    // somebody else's reference.
    std::map<api::FileId, int> files;
    std::int64_t lastActiveMs = 0;
};

// What a dropped client releases: its searches to cancel and one close per
// owned file reference.
struct ClientResources {
    std::vector<api::SearchId> searches;
    std::vector<api::FileId> files;
};

class ClientRegistry {
public:
    std::shared_ptr<ClientInfo> create( std::string name, std::string role );
    std::shared_ptr<ClientInfo> find( const std::string& id ) const;
    // Returns the existing client or creates a new one. An unknown non-empty id
    // is not trusted (ids are server-assigned) and is replaced.
    std::shared_ptr<ClientInfo> getOrCreate( const std::string& id, std::string role );

    std::vector<ClientInfo> list() const;

    // Removes the client and returns what must be released on the Session.
    ClientResources remove( const std::string& id );

    void addSearch( const std::string& id, api::SearchId searchId );
    void removeSearch( const std::string& id, api::SearchId searchId );
    bool ownsSearch( const std::string& id, api::SearchId searchId ) const;

    void addFile( const std::string& id, api::FileId fileId );
    void removeFile( const std::string& id, api::FileId fileId );
    bool ownsFile( const std::string& id, api::FileId fileId ) const;

    void touch( const std::string& id );

private:
    static std::vector<std::string> capabilitiesFor( const std::string& role );
    static std::int64_t nowMs();

    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<ClientInfo>> clients_;
    std::uint64_t nextId_ = 1;
};

} // namespace klogg::server
