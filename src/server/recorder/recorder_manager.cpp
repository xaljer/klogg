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

#include "recorder_manager.h"

#include <algorithm>
#include <filesystem>
#include <utility>
#include <vector>

namespace klogg::server {

namespace {

std::string substituteFile( const std::string& command, const std::string& filePath )
{
    std::string result = command;
    const std::string placeholder = "{file}";
    std::size_t position = 0;
    while ( ( position = result.find( placeholder, position ) ) != std::string::npos ) {
        result.replace( position, placeholder.size(), filePath );
        position += filePath.size();
    }
    return result;
}

std::string workingDirectoryFor( const std::string& filePath )
{
    if ( filePath.find( "://" ) != std::string::npos ) {
        return {};
    }
    const auto separator = filePath.find_last_of( "/\\" );
    if ( separator == std::string::npos || separator == 0 ) {
        return {};
    }
    return filePath.substr( 0, separator );
}

std::string trim( std::string value )
{
    const auto isSpace = []( unsigned char character ) {
        return character == ' ' || character == '\t' || character == '\r' || character == '\n';
    };
    value.erase( value.begin(),
                 std::find_if( value.begin(), value.end(),
                               [ & ]( unsigned char character ) { return !isSpace( character ); } ) );
    value.erase( std::find_if( value.rbegin(), value.rend(),
                               [ & ]( unsigned char character ) { return !isSpace( character ); } )
                     .base(),
                 value.end() );
    return value;
}

} // namespace

RecorderManager::~RecorderManager()
{
    stopAll();
    std::vector<std::shared_ptr<Entry>> entries;
    {
        std::lock_guard lock( mutex_ );
        for ( auto& [ id, entry ] : entries_ ) {
            entries.push_back( entry );
        }
    }
    for ( const auto& entry : entries ) {
        if ( entry->worker.joinable() ) {
            entry->worker.join();
        }
    }
}

std::optional<RecorderStatus> RecorderManager::start( const std::string& command,
                                                     const std::string& filePath,
                                                     const std::string& owner,
                                                     std::string& error )
{
    const auto resolvedCommand = substituteFile( command, filePath );
    if ( trim( resolvedCommand ).empty() ) {
        error = "recorder command is empty";
        return std::nullopt;
    }
    if ( filePath.empty() ) {
        error = "recorder target file is empty";
        return std::nullopt;
    }
    if ( filePath.find( "://" ) != std::string::npos ) {
        error = "recorder cannot write to a URL source";
        return std::nullopt;
    }

    const auto workingDirectory = workingDirectoryFor( filePath );
    std::string processError;
    auto process = startRecorderProcess( resolvedCommand, workingDirectory, processError );
    if ( !process ) {
        error = processError.empty() ? "could not start recorder command" : processError;
        return std::nullopt;
    }

    auto entry = std::make_shared<Entry>();
    entry->owner = owner;
    entry->process = std::move( process );
    entry->status.state = "running";
    entry->status.pid = entry->process->pid();

    {
        std::lock_guard lock( mutex_ );
        entry->status.id = nextId_++;
        entries_.emplace( entry->status.id, entry );
    }

    try {
        entry->worker = std::thread( [ entry ]() {
            const int exitCode = entry->process->wait();
            std::lock_guard lock( entry->mutex );
            entry->status.exitCode = exitCode;
            if ( entry->stopRequested ) {
                entry->status.state = "stopped";
            }
            else if ( exitCode == 0 ) {
                entry->status.state = "finished";
            }
            else {
                entry->status.state = "error";
                entry->status.error
                    = "command exited with code " + std::to_string( exitCode );
            }
        } );
    } catch ( const std::exception& exception ) {
        entry->process->terminate();
        error = std::string( "could not start recorder worker: " ) + exception.what();
        std::lock_guard lock( mutex_ );
        entries_.erase( entry->status.id );
        return std::nullopt;
    }

    std::lock_guard lock( entry->mutex );
    return entry->status;
}

bool RecorderManager::stop( std::uint64_t id, std::string& error )
{
    std::shared_ptr<Entry> entry;
    {
        std::lock_guard lock( mutex_ );
        const auto found = entries_.find( id );
        if ( found == entries_.end() ) {
            error = "recorder process not found";
            return false;
        }
        entry = found->second;
    }

    std::shared_ptr<RecorderProcess> process;
    {
        std::lock_guard lock( entry->mutex );
        if ( entry->status.state != "running" ) {
            return true;
        }
        entry->stopRequested = true;
        process = entry->process;
    }
    if ( process ) {
        process->terminate();
    }
    return true;
}

std::optional<RecorderStatus> RecorderManager::status( std::uint64_t id ) const
{
    std::shared_ptr<Entry> entry;
    {
        std::lock_guard lock( mutex_ );
        const auto found = entries_.find( id );
        if ( found == entries_.end() ) {
            return std::nullopt;
        }
        entry = found->second;
    }
    std::lock_guard lock( entry->mutex );
    return entry->status;
}

void RecorderManager::stopByOwner( const std::string& owner )
{
    if ( owner.empty() ) {
        return;
    }
    std::vector<std::uint64_t> ids;
    {
        std::lock_guard lock( mutex_ );
        for ( const auto& [ id, entry ] : entries_ ) {
            if ( entry->owner == owner ) {
                ids.push_back( id );
            }
        }
    }
    for ( const auto id : ids ) {
        std::string ignored;
        stop( id, ignored );
    }
}

void RecorderManager::stopAll()
{
    std::vector<std::uint64_t> ids;
    {
        std::lock_guard lock( mutex_ );
        for ( const auto& [ id, entry ] : entries_ ) {
            ids.push_back( id );
        }
    }
    for ( const auto id : ids ) {
        std::string ignored;
        stop( id, ignored );
    }
}

} // namespace klogg::server
