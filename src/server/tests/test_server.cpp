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

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "httplib.h"
#include "klogg/server/server.h"

using namespace klogg;
using nlohmann::json;

namespace {

struct ApiResult {
    int status = 0;
    json body;
};

struct TempDir {
    std::filesystem::path path;

    TempDir()
    {
        path
            = std::filesystem::temp_directory_path()
              / ( "klogg_server_test_"
                  + std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() ) );
        std::filesystem::create_directories( path );
    }

    ~TempDir()
    {
        std::error_code error;
        std::filesystem::remove_all( path, error );
    }
};

struct TestServer {
    std::unique_ptr<api::Session> session;
    std::unique_ptr<server::Server> server;
    int port = 0;

    explicit TestServer( server::ServerOptions options = {} )
    {
        auto createdSession = api::Session::create();
        REQUIRE( createdSession.has_value() );
        session = std::move( *createdSession );

        options.listenAddress = "127.0.0.1";
        options.port = 0;

        auto createdServer = server::Server::create( *session, options );
        REQUIRE( createdServer.has_value() );
        server = std::move( *createdServer );

        const auto started = server->start();
        if ( !started ) {
            FAIL( started.error().message );
        }
        port = server->port();
    }

    ~TestServer()
    {
        if ( server ) {
            server->stop();
        }
    }
};

std::unique_ptr<httplib::Client> makeClient( int port )
{
    auto client = std::make_unique<httplib::Client>( "127.0.0.1", port );
    client->set_connection_timeout( 5, 0 );
    client->set_read_timeout( 15, 0 );
    client->set_write_timeout( 15, 0 );
    return client;
}

ApiResult post( httplib::Client& client, const std::string& method, const json& params,
                const std::string& clientId = {}, const std::string& token = {} )
{
    httplib::Headers headers;
    if ( !clientId.empty() ) {
        headers.emplace( "X-Klogg-Client-Id", clientId );
    }
    if ( !token.empty() ) {
        headers.emplace( "Authorization", "Bearer " + token );
    }

    const auto result = client.Post( "/api/" + method, headers, params.dump(), "application/json" );
    if ( !result ) {
        FAIL( "HTTP request failed with error " << static_cast<int>( result.error() ) );
        return {};
    }
    return { result->status, json::parse( result->body ) };
}

std::string connectClient( httplib::Client& client, const std::string& role = "human" )
{
    const auto result = post( client, "session.connect", { { "name", "test" }, { "role", role } } );
    REQUIRE( result.status == 200 );
    REQUIRE( result.body[ "ok" ].get<bool>() );
    return result.body[ "value" ][ "clientId" ].get<std::string>();
}

std::filesystem::path writeLog( const std::filesystem::path& directory, const std::string& name,
                                int lines )
{
    const auto path = directory / name;
    std::ofstream output( path );
    REQUIRE( output.is_open() );
    for ( int i = 0; i < lines; ++i ) {
        if ( i % 7 == 0 ) {
            output << "2026-09-27T12:00:00.000Z ERROR request " << i << " timeout\n";
        }
        else {
            output << "2026-09-27T12:00:00.000Z INFO line " << i << "\n";
        }
    }
    return path;
}

api::FileId openFile( httplib::Client& client, const std::string& clientId,
                      const std::filesystem::path& path )
{
    const auto result = post( client, "file.open", { { "path", path.string() } }, clientId );
    REQUIRE( result.status == 200 );
    REQUIRE( result.body[ "ok" ].get<bool>() );
    return result.body[ "value" ][ "fileId" ].get<api::FileId>();
}

void waitForSearch( httplib::Client& client, const std::string& clientId, api::SearchId searchId )
{
    for ( int attempt = 0; attempt < 200; ++attempt ) {
        const auto result
            = post( client, "search.progress", { { "searchId", searchId } }, clientId );
        REQUIRE( result.status == 200 );
        const auto& value = result.body[ "value" ];
        if ( value[ "progress" ].get<double>() >= 1.0 ) {
            return;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    }
    FAIL( "search did not complete" );
}

} // namespace

TEST_CASE( "server: highlighter and quick palette configuration", "[server]" )
{
    TestServer testServer;
    auto client = makeClient( testServer.port );
    const auto clientId = connectClient( *client );

    const auto rules = post(
        *client, "highlighters.set",
        { { "rules",
            { { { "name", "errors" },
                { "pattern", "ERROR" },
                { "useRegex", false },
                { "caseSensitive", true },
                { "color", "#fff38ba8" },
                { "useThemeBack", true },
                { "enabled", true } } } } },
        clientId );
    REQUIRE( rules.status == 200 );
    REQUIRE( rules.body[ "ok" ].get<bool>() );

    const auto quick = post(
        *client, "quickHighlighters.set",
        { { "defs",
            { { { "name", "red" }, { "color", "#fff38ba8" }, { "backColor", "#55f38ba8" },
                { "useInCycle", true } } } } },
        clientId );
    REQUIRE( quick.status == 200 );
    REQUIRE( quick.body[ "ok" ].get<bool>() );

    const auto malformed
        = post( *client, "highlighters.set", { { "rules", { json::object() } } }, clientId );
    REQUIRE( malformed.status == 400 );
    REQUIRE( malformed.body[ "error" ][ "code" ] == "InvalidArgument" );
}

TEST_CASE( "server: system.info and unknown method", "[server]" )
{
    TestServer testServer;
    auto client = makeClient( testServer.port );

    const auto info = post( *client, "system.info", json::object() );
    REQUIRE( info.status == 200 );
    REQUIRE( info.body[ "value" ][ "apiVersion" ] == "1.0" );

    const auto missing = post( *client, "no.such.method", json::object() );
    REQUIRE( missing.status == 404 );
    REQUIRE( missing.body[ "ok" ].get<bool>() == false );
    REQUIRE( missing.body[ "error" ][ "code" ] == "MethodNotFound" );
}

TEST_CASE( "server: file, search, marks, flags and viewport", "[server]" )
{
    TempDir directory;
    const auto logPath = writeLog( directory.path, "app.log", 120 );

    TestServer testServer;
    auto client = makeClient( testServer.port );
    const auto clientId = connectClient( *client );

    const auto fileId = openFile( *client, clientId, logPath );

    const auto list = post( *client, "file.list", json::object(), clientId );
    REQUIRE( list.status == 200 );
    REQUIRE( list.body[ "value" ][ "files" ].size() == 1 );

    const auto stat = post( *client, "file.stat", { { "fileId", fileId } }, clientId );
    REQUIRE( stat.status == 200 );
    REQUIRE( stat.body[ "value" ][ "sizeBytes" ].get<int64_t>() > 0 );

    const auto lines = post( *client, "lines.get",
                             { { "fileId", fileId }, { "from", 0 }, { "count", 10 } }, clientId );
    REQUIRE( lines.status == 200 );
    REQUIRE( lines.body[ "value" ][ "lines" ].size() == 10 );
    REQUIRE( lines.body[ "value" ][ "lines" ][ 0 ][ "num" ] == 0 );

    const auto search = post(
        *client, "search.run",
        { { "fileId", fileId }, { "params", { { "pattern", "ERROR" }, { "isRegex", false } } } },
        clientId );
    REQUIRE( search.status == 200 );
    const auto searchId = search.body[ "value" ][ "searchId" ].get<api::SearchId>();

    waitForSearch( *client, clientId, searchId );

    const auto matches
        = post( *client, "search.matches",
                { { "searchId", searchId }, { "from", 0 }, { "count", 200 } }, clientId );
    REQUIRE( matches.status == 200 );
    REQUIRE( matches.body[ "value" ][ "matches" ].size() == 18 );

    const auto filtered
        = post( *client, "search.filteredLineCount", { { "searchId", searchId } }, clientId );
    REQUIRE( filtered.status == 200 );
    REQUIRE( filtered.body[ "value" ][ "lineCount" ] == 18 );

    const auto marksAdd
        = post( *client, "marks.add", { { "fileId", fileId }, { "lines", { 1, 3 } } }, clientId );
    REQUIRE( marksAdd.status == 200 );

    const auto marks = post( *client, "marks.list", { { "fileId", fileId } }, clientId );
    REQUIRE( marks.status == 200 );
    REQUIRE( marks.body[ "value" ][ "marks" ].size() == 2 );
    REQUIRE( marks.body[ "value" ][ "marks" ][ 0 ] == 1 );
    REQUIRE( marks.body[ "value" ][ "marks" ][ 1 ] == 3 );

    const auto flags
        = post( *client, "flags.get",
                { { "fileId", fileId }, { "searchId", searchId }, { "from", 0 }, { "count", 10 } },
                clientId );
    REQUIRE( flags.status == 200 );
    REQUIRE( flags.body[ "value" ][ "flags" ].size() == 10 );
    // line 0 matches ERROR, line 1 carries a mark
    REQUIRE( ( flags.body[ "value" ][ "flags" ][ 0 ].get<int>() & 2 ) != 0 );
    REQUIRE( ( flags.body[ "value" ][ "flags" ][ 1 ].get<int>() & 1 ) != 0 );

    const json viewportOptions = {
        { "wrapColumns", 0 },
        { "visibleColumns", 200 },
        { "firstDisplayLineNumber", 1 },
        { "palette",
          { { "text", "#ffcdd6f4" },
            { "base", "#ff1e1e2e" },
            { "dimmedText", "#ff6c7086" },
            { "selectionText", "#ff1e1e2e" },
            { "selectionBack", "#ff89b4fa" } } },
    };
    const auto viewport = post( *client, "render.viewport",
                                { { "fileId", fileId },
                                  { "searchId", searchId },
                                  { "firstLine", 0 },
                                  { "visibleLines", 10 },
                                  { "options", viewportOptions } },
                                clientId );
    REQUIRE( viewport.status == 200 );
    REQUIRE( viewport.body[ "value" ][ "lines" ].size() == 10 );
    REQUIRE( viewport.body[ "value" ][ "lines" ][ 0 ][ "text" ].get<std::string>().find( "ERROR" )
             != std::string::npos );
    REQUIRE_FALSE( viewport.body[ "value" ][ "lines" ][ 0 ][ "chunks" ].empty() );

    const auto html = post( *client, "render.linesHtml",
                            { { "fileId", fileId },
                              { "searchId", searchId },
                              { "lines", { 2, 0 } },
                              { "options",
                                { { "searchStartIndex", 0 }, { "searchEndIndex", 120 } } } },
                            clientId );
    REQUIRE( html.status == 200 );
    REQUIRE( html.body[ "ok" ].get<bool>() );
    const auto htmlText = html.body[ "value" ][ "text" ].get<std::string>();
    REQUIRE( htmlText.find( "INFO line 2" ) < htmlText.find( "ERROR request 0" ) );
    REQUIRE( html.body[ "value" ][ "html" ].get<std::string>().find(
                 "<pre style=\"font-family:monospace; white-space:pre; margin:0\">" )
             == 0 );

    const auto closed = post( *client, "file.close", { { "fileId", fileId } }, clientId );
    REQUIRE( closed.status == 200 );
}

TEST_CASE( "server: error mapping", "[server]" )
{
    TempDir directory;
    const auto logPath = writeLog( directory.path, "pattern.log", 5 );

    TestServer testServer;
    auto client = makeClient( testServer.port );

    const auto badFile = post( *client, "file.info", { { "fileId", 999999 } } );
    REQUIRE( badFile.status == 404 );
    REQUIRE( badFile.body[ "error" ][ "code" ] == "FileNotOpen" );

    const auto badBody = client->Post( "/api/system.info", "{", "application/json" );
    REQUIRE( badBody );
    REQUIRE( badBody->status == 400 );
    const auto parsed = json::parse( badBody->body );
    REQUIRE( parsed[ "error" ][ "code" ] == "InvalidRequest" );

    const auto clientId = connectClient( *client );

    const auto badEncoding
        = post( *client, "file.open",
                { { "path", logPath.string() }, { "encoding", "no-such-encoding" } }, clientId );
    REQUIRE( badEncoding.status == 400 );
    REQUIRE( badEncoding.body[ "error" ][ "code" ] == "EncodingError" );

    const auto invalidPattern = post( *client, "search.validatePattern",
                                      { { "pattern", "(" }, { "isRegex", true } }, clientId );
    REQUIRE( invalidPattern.status == 200 );
    REQUIRE_FALSE( invalidPattern.body[ "value" ][ "valid" ].get<bool>() );

    openFile( *client, clientId, logPath );
}

TEST_CASE( "server: invalid UTF-8 in line text is replaced, not fatal", "[server]" )
{
    TempDir directory;
    const auto path = directory.path / "binary.log";
    {
        std::ofstream output( path, std::ios::binary );
        REQUIRE( output.is_open() );
        output << "ok line\n";
        output << "bad \xFF\xFE bytes\n";
    }

    TestServer testServer;
    auto client = makeClient( testServer.port );
    const auto clientId = connectClient( *client );
    const auto fileId = openFile( *client, clientId, path );

    const auto lines = post( *client, "lines.get",
                             { { "fileId", fileId }, { "from", 0 }, { "count", 2 } }, clientId );
    REQUIRE( lines.status == 200 );
    REQUIRE( lines.body[ "value" ][ "lines" ].size() == 2 );
    REQUIRE( lines.body[ "value" ][ "lines" ][ 1 ][ "text" ].get<std::string>().find( "bad" )
             != std::string::npos );
}

TEST_CASE( "server: bearer token", "[server]" )
{
    server::ServerOptions options;
    options.token = "secret";
    TestServer testServer( options );
    auto client = makeClient( testServer.port );

    const auto unauthorized = post( *client, "system.info", json::object() );
    REQUIRE( unauthorized.status == 401 );
    REQUIRE( unauthorized.body[ "error" ][ "code" ] == "Unauthorized" );

    const auto authorized = post( *client, "system.info", json::object(), {}, "secret" );
    REQUIRE( authorized.status == 200 );

    const auto wrong = post( *client, "system.info", json::object(), {}, "nope" );
    REQUIRE( wrong.status == 401 );
}

TEST_CASE( "server: CORS preflight", "[server]" )
{
    TestServer testServer;
    auto client = makeClient( testServer.port );

    const auto result
        = client->Options( "/api/system.info", { { "Origin", "http://localhost:5173" } } );
    REQUIRE( result );
    REQUIRE( result->status == 204 );
    REQUIRE( result->get_header_value( "Access-Control-Allow-Origin" ) == "http://localhost:5173" );
}

TEST_CASE( "server: root allowlist", "[server]" )
{
    TempDir directory;
    const auto root = directory.path / "root";
    std::filesystem::create_directories( root );
    const auto inside = writeLog( root, "inside.log", 5 );
    const auto outside = writeLog( directory.path, "outside.log", 5 );

    server::ServerOptions options;
    options.root = root;
    TestServer testServer( options );
    auto client = makeClient( testServer.port );
    const auto clientId = connectClient( *client );

    const auto opened = post( *client, "file.open", { { "path", inside.string() } }, clientId );
    REQUIRE( opened.status == 200 );

    const auto rejected = post( *client, "file.open", { { "path", outside.string() } }, clientId );
    REQUIRE( rejected.status == 403 );
    REQUIRE( rejected.body[ "error" ][ "code" ] == "Forbidden" );

    const auto listing = post( *client, "fs.list", { { "path", root.string() } }, clientId );
    REQUIRE( listing.status == 200 );
    REQUIRE( listing.body[ "value" ][ "entries" ].size() == 1 );

    const auto outsideListing
        = post( *client, "fs.list", { { "path", directory.path.string() } }, clientId );
    REQUIRE( outsideListing.status == 403 );
    REQUIRE( outsideListing.body[ "error" ][ "code" ] == "Forbidden" );
}

TEST_CASE( "server: handles belong to the creating client", "[server]" )
{
    TempDir directory;
    const auto logPath = writeLog( directory.path, "owned.log", 20 );

    TestServer testServer;
    auto client = makeClient( testServer.port );
    const auto human = connectClient( *client, "human" );
    const auto ai = connectClient( *client, "ai" );

    const auto fileId = openFile( *client, human, logPath );

    const auto foreignClose = post( *client, "file.close", { { "fileId", fileId } }, ai );
    REQUIRE( foreignClose.status == 403 );
    REQUIRE( foreignClose.body[ "error" ][ "code" ] == "Forbidden" );

    const auto search = post(
        *client, "search.run",
        { { "fileId", fileId }, { "params", { { "pattern", "ERROR" }, { "isRegex", false } } } },
        human );
    REQUIRE( search.status == 200 );
    const auto searchId = search.body[ "value" ][ "searchId" ].get<api::SearchId>();

    const auto foreignCancel = post( *client, "search.cancel", { { "searchId", searchId } }, ai );
    REQUIRE( foreignCancel.status == 403 );

    waitForSearch( *client, human, searchId );
    const auto readable = post( *client, "search.matches",
                                { { "searchId", searchId }, { "from", 0 }, { "count", 10 } }, ai );
    REQUIRE( readable.status == 200 );

    const auto ownerClose = post( *client, "file.close", { { "fileId", fileId } }, human );
    REQUIRE( ownerClose.status == 200 );
}

TEST_CASE( "server: SSE delivers core events", "[server][sse]" )
{
    TempDir directory;
    const auto logPath = writeLog( directory.path, "sse.log", 20 );

    TestServer testServer;
    auto client = makeClient( testServer.port );
    const auto clientId = connectClient( *client );
    const auto fileId = openFile( *client, clientId, logPath );

    std::mutex mutex;
    std::condition_variable cv;
    bool helloSeen = false;
    bool marksSeen = false;
    std::string buffer;

    std::thread sseThread( [ & ] {
        httplib::Client sse( "127.0.0.1", testServer.port );
        sse.set_read_timeout( 15, 0 );
        (void)sse.Get( "/api/events?clientId=" + clientId,
                       [ & ]( const char* data, std::size_t length ) {
                           std::lock_guard lock( mutex );
                           buffer.append( data, length );
                           if ( buffer.find( "client.hello" ) != std::string::npos ) {
                               helloSeen = true;
                           }
                           if ( buffer.find( "marks.changed" ) != std::string::npos ) {
                               marksSeen = true;
                           }
                           cv.notify_all();
                           return !marksSeen;
                       } );
    } );

    {
        std::unique_lock lock( mutex );
        REQUIRE( cv.wait_for( lock, std::chrono::seconds( 10 ), [ & ] { return helloSeen; } ) );
    }

    const auto added
        = post( *client, "marks.add", { { "fileId", fileId }, { "lines", { 0 } } }, clientId );
    REQUIRE( added.status == 200 );

    {
        std::unique_lock lock( mutex );
        REQUIRE( cv.wait_for( lock, std::chrono::seconds( 10 ), [ & ] { return marksSeen; } ) );
    }

    sseThread.join();
    REQUIRE( buffer.find( "marks.changed" ) != std::string::npos );
}
