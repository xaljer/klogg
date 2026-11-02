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

#include "recorder_config.h"

#include <algorithm>

#include "settings.h"
#include "userdata.h"

namespace klogg::server {

namespace {

constexpr const char* CommandsKey = "recorderCommands";
constexpr const char* BindingsKey = "recorderFileBindings";

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

std::vector<RecorderCommandConfig> RecorderConfig::commands() const
{
    std::lock_guard lock( mutex_ );
    auto& settings = platform::userdata::config();
    std::vector<RecorderCommandConfig> result;
    const auto count = settings.beginReadArray( CommandsKey );
    for ( int index = 0; index < count; ++index ) {
        settings.setArrayIndex( index );
        const auto name = trim( settings.getString( "name" ) );
        const auto command = trim( settings.getString( "command" ) );
        if ( !name.empty() && !command.empty() ) {
            result.push_back( { name, command } );
        }
    }
    settings.endArray();
    return result;
}

std::optional<std::string> RecorderConfig::commandForName( const std::string& name ) const
{
    const auto all = commands();
    const auto found = std::find_if( all.cbegin(), all.cend(),
                                     [ & ]( const auto& command ) { return command.name == name; } );
    if ( found == all.cend() ) {
        return std::nullopt;
    }
    return found->command;
}

bool RecorderConfig::replaceCommands( const std::vector<RecorderCommandConfig>& commands,
                                      std::string& error )
{
    std::lock_guard lock( mutex_ );
    auto& settings = platform::userdata::config();
    settings.remove( CommandsKey );
    settings.beginWriteArray( CommandsKey );
    int index = 0;
    for ( const auto& command : commands ) {
        const auto name = trim( command.name );
        const auto value = trim( command.command );
        if ( name.empty() || value.empty() ) {
            continue;
        }
        settings.setArrayIndex( index++ );
        settings.setString( "name", name );
        settings.setString( "command", value );
    }
    settings.endArray();
    if ( !settings.save() ) {
        error = "could not save recorder commands: " + settings.path().string();
        return false;
    }
    return true;
}

std::vector<std::pair<std::string, std::string>> RecorderConfig::bindings() const
{
    std::lock_guard lock( mutex_ );
    auto& settings = platform::userdata::session();
    std::vector<std::pair<std::string, std::string>> result;
    const auto count = settings.beginReadArray( BindingsKey );
    for ( int index = 0; index < count; ++index ) {
        settings.setArrayIndex( index );
        const auto file = trim( settings.getString( "file" ) );
        const auto command = trim( settings.getString( "command" ) );
        if ( !file.empty() ) {
            result.emplace_back( file, command );
        }
    }
    settings.endArray();
    return result;
}

std::string RecorderConfig::bindingForFile( const std::string& filePath ) const
{
    const auto all = bindings();
    const auto found = std::find_if(
        all.cbegin(), all.cend(),
        [ & ]( const auto& binding ) { return binding.first == filePath; } );
    return found == all.cend() ? std::string{} : found->second;
}

bool RecorderConfig::setBinding( const std::string& filePath, const std::string& commandName,
                                 std::string& error )
{
    std::lock_guard lock( mutex_ );
    if ( filePath.empty() ) {
        error = "file path is empty";
        return false;
    }

    auto all = bindings();
    if ( !commandName.empty() ) {
        const auto commandsList = commands();
        const auto exists = std::any_of( commandsList.cbegin(), commandsList.cend(),
                                         [ & ]( const auto& command ) {
                                             return command.name == commandName;
                                         } );
        if ( !exists ) {
            error = "recorder command not found: " + commandName;
            return false;
        }
    }

    all.erase( std::remove_if( all.begin(), all.end(),
                               [ & ]( const auto& binding ) { return binding.first == filePath; } ),
               all.end() );
    if ( !commandName.empty() ) {
        all.emplace_back( filePath, commandName );
    }

    auto& settings = platform::userdata::session();
    settings.remove( BindingsKey );
    settings.beginWriteArray( BindingsKey );
    for ( std::size_t index = 0; index < all.size(); ++index ) {
        settings.setArrayIndex( static_cast<int>( index ) );
        settings.setString( "file", all[index].first );
        settings.setString( "command", all[index].second );
    }
    settings.endArray();
    if ( !settings.save() ) {
        error = "could not save recorder bindings: " + settings.path().string();
        return false;
    }
    return true;
}

} // namespace klogg::server
