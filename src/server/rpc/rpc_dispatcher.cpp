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

#include "rpc_dispatcher.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "klogg_version.h"

#include "compression.h"
#include "httplib.h"
#include "json_convert.h"
#include "paths.h"

namespace klogg::server {

using nlohmann::json;

namespace {


api::FileId fileId( const json& params )
{
    return params.at( "fileId" ).get<api::FileId>();
}

api::SearchId searchId( const json& params )
{
    return params.at( "searchId" ).get<api::SearchId>();
}

std::optional<api::SearchId> optionalSearchId( const json& params )
{
    if ( !params.contains( "searchId" ) || params[ "searchId" ].is_null() ) {
        return std::nullopt;
    }
    return params[ "searchId" ].get<api::SearchId>();
}

std::vector<LineNumber> requestedLines( const json& params, std::size_t maxLines )
{
    std::vector<LineNumber> lines;
    const auto& source = params.at( "lines" );
    const auto count = std::min( source.size(), maxLines );
    lines.reserve( count );
    for ( std::size_t i = 0; i < count; ++i ) {
        lines.emplace_back( source[ i ].get<uint64_t>() );
    }
    return lines;
}

json linesToJson( const std::vector<api::LineData>& lines )
{
    json result = json::array();
    for ( const auto& line : lines ) {
        result.push_back( toJson( line ) );
    }
    return result;
}

json lineNumbersToJson( const std::vector<LineNumber>& lines )
{
    json result = json::array();
    for ( const auto& line : lines ) {
        result.push_back( line.get() );
    }
    return result;
}

json optionalLineToJson( const std::optional<LineNumber>& line )
{
    return { { "line", line ? json( line->get() ) : json( nullptr ) } };
}

json optionalIndexToJson( const std::optional<uint64_t>& index )
{
    return { { "index", index ? json( *index ) : json( nullptr ) } };
}

json flagsToJson( const std::vector<uint8_t>& flags )
{
    json result = json::array();
    for ( const auto flag : flags ) {
        result.push_back( static_cast<int>( flag ) );
    }
    return result;
}

uint64_t clampedCount( const json& params, std::size_t maximum )
{
    const auto count = params.value( "count", uint64_t{ 0 } );
    return std::min<uint64_t>( count, maximum );
}

std::string lowerAscii( std::string value )
{
    std::transform( value.begin(), value.end(), value.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return value;
}

std::string toUtf8( const std::filesystem::path& path )
{
    const auto text = path.u8string();
    return std::string( reinterpret_cast<const char*>( text.data() ), text.size() );
}

std::string percentDecode( const std::string& value )
{
    std::string result;
    result.reserve( value.size() );
    for ( std::size_t index = 0; index < value.size(); ++index ) {
        if ( value[ index ] == '%' && index + 2 < value.size() ) {
            const auto hex = value.substr( index + 1, 2 );
            char* end = nullptr;
            const auto decoded = std::strtol( hex.c_str(), &end, 16 );
            if ( end != nullptr && *end == '\0' ) {
                result.push_back( static_cast<char>( decoded ) );
                index += 2;
                continue;
            }
        }
        result.push_back( value[ index ] );
    }
    return result;
}

// The URL tail is the name the desktop build uses for the temporary download.
std::string downloadFileName( const std::string& url )
{
    const auto schemeEnd = url.find( "://" );
    const auto pathStart = schemeEnd == std::string::npos ? std::string::npos
                                                          : url.find( '/', schemeEnd + 3 );
    auto path = pathStart == std::string::npos ? std::string{} : url.substr( pathStart );
    const auto cut = path.find_first_of( "?#" );
    if ( cut != std::string::npos ) {
        path = path.substr( 0, cut );
    }
    const auto slash = path.find_last_of( '/' );
    auto name = percentDecode( slash == std::string::npos ? path : path.substr( slash + 1 ) );

    for ( auto& character : name ) {
        if ( static_cast<unsigned char>( character ) < 0x20
             || std::strchr( "<>:\"/\\|?*", character ) != nullptr ) {
            character = '_';
        }
    }
    while ( !name.empty() && ( name.back() == '.' || name.back() == ' ' ) ) {
        name.pop_back();
    }
    if ( name.empty() || name == "." || name == ".." ) {
        name = "download.log";
    }
    return name;
}

api::HighlightRule parseHighlightRule( const json& item )
{
    api::HighlightRule rule;
    rule.name = item.value( "name", std::string{} );
    rule.pattern = item.at( "pattern" ).get<std::string>();
    rule.useRegex = item.value( "useRegex", true );
    rule.caseSensitive = item.value( "caseSensitive", true );
    rule.color = item.value( "color", std::string{ "#ffff0000" } );
    rule.backColor = item.value( "backColor", std::string{} );
    rule.useThemeBack = item.value( "useThemeBack", true );
    rule.onlyMatch = item.value( "onlyMatch", false );
    rule.variateColors = item.value( "variateColors", false );
    rule.colorVariance = item.value( "colorVariance", 15 );
    rule.enabled = item.value( "enabled", true );
    return rule;
}

api::QuickHighlighterDef parseQuickHighlighterDef( const json& item )
{
    api::QuickHighlighterDef def;
    def.name = item.value( "name", std::string{} );
    def.color = item.value( "color", std::string{ "#ffff0000" } );
    def.backColor = item.value( "backColor", std::string{} );
    def.useInCycle = item.value( "useInCycle", true );
    return def;
}

struct ParsedHttpUrl {
    std::string host;
    int port = 80;
    std::string target;
};

std::optional<ParsedHttpUrl> parseHttpUrl( const std::string& url )
{
    constexpr const char* prefix = "http://";
    if ( url.compare( 0, std::char_traits<char>::length( prefix ), prefix ) != 0 ) {
        return std::nullopt;
    }
    auto rest = url.substr( std::char_traits<char>::length( prefix ) );
    const auto slash = rest.find_first_of( "/?#" );
    auto authority = slash == std::string::npos ? rest : rest.substr( 0, slash );
    auto target = slash == std::string::npos ? std::string{ "/" } : rest.substr( slash );
    if ( target.empty() || target.front() != '/' ) {
        target.insert( target.begin(), '/' );
    }
    const auto fragment = target.find( '#' );
    if ( fragment != std::string::npos ) {
        target.erase( fragment );
    }
    if ( authority.empty() ) {
        return std::nullopt;
    }
    ParsedHttpUrl parsed;
    const auto colon = authority.rfind( ':' );
    if ( colon != std::string::npos ) {
        parsed.host = authority.substr( 0, colon );
        const auto port = authority.substr( colon + 1 );
        try {
            parsed.port = std::stoi( port );
        }
        catch ( ... ) {
            return std::nullopt;
        }
    }
    else {
        parsed.host = std::move( authority );
    }
    if ( parsed.host.empty() || parsed.host.find( '@' ) != std::string::npos
         || parsed.port <= 0 || parsed.port > 65535 ) {
        return std::nullopt;
    }
    parsed.target = std::move( target );
    return parsed;
}

} // namespace

RpcDispatcher::RpcDispatcher( api::Session& session, ClientRegistry& registry,
                              const ServerOptions& options )
    : session_( session )
    , registry_( registry )
    , options_( options )
{
}

void RpcDispatcher::stopClient( const std::string& clientId )
{
    recorder_.stopByOwner( clientId );
}

RpcResponse RpcDispatcher::ok( json value ) const
{
    return { 200, { { "ok", true }, { "value", std::move( value ) } }, {} };
}

RpcResponse RpcDispatcher::fail( const api::Error& error ) const
{
    return fail( httpStatus( error.code ), errorCodeName( error.code ), error.message );
}

RpcResponse RpcDispatcher::fail( int status, std::string code, std::string message ) const
{
    return { status,
             { { "ok", false },
               { "error",
                 { { "code", std::move( code ) }, { "message", std::move( message ) } } } },
             {} };
}

bool RpcDispatcher::canUseRecorder( const std::string& clientId ) const
{
    const auto client = registry_.find( clientId );
    return client
           && std::find( client->capabilities.cbegin(), client->capabilities.cend(), "recorder" )
                  != client->capabilities.cend();
}

bool RpcDispatcher::pathAllowed( const std::string& path ) const
{    if ( options_.root.empty() ) {
        return true;
    }
    if ( path.find( "://" ) != std::string::npos ) {
        return false;
    }

    std::error_code error;
    auto root = std::filesystem::weakly_canonical( options_.root, error );
    if ( error ) {
        root = std::filesystem::absolute( options_.root, error ).lexically_normal();
    }
    auto candidate = std::filesystem::weakly_canonical( std::filesystem::path( path ), error );
    if ( error ) {
        candidate
            = std::filesystem::absolute( std::filesystem::path( path ), error ).lexically_normal();
    }

    auto rootText = root.generic_string();
    auto candidateText = candidate.generic_string();
#ifdef _WIN32
    rootText = lowerAscii( std::move( rootText ) );
    candidateText = lowerAscii( std::move( candidateText ) );
#endif

    if ( candidateText == rootText ) {
        return true;
    }
    if ( candidateText.size() <= rootText.size() ) {
        return false;
    }
    if ( candidateText.compare( 0, rootText.size(), rootText ) != 0 ) {
        return false;
    }
    const auto separator = candidateText[ rootText.size() ];
    return separator == '/' || separator == '\\';
}

RpcResponse RpcDispatcher::dispatch( const std::string& method, const std::string& clientId,
                                     const json& params )
{
    try {
        if ( method == "system.info" ) {
            return ok( {
                { "apiVersion", kloggApiVersion() },
                { "serverVersion", session_.version() },
                { "protocol", "http+sse" },
                { "capabilities", { "read", "search", "mark", "view", "config", "recorder" } },
            } );
        }

        if ( method == "session.connect" ) {
            auto info = registry_.create( params.value( "name", std::string( "anonymous" ) ),
                                          params.value( "role", std::string( "human" ) ) );
            auto response = ok( {
                { "clientId", info->id },
                { "apiVersion", kloggApiVersion() },
                { "serverVersion", session_.version() },
                { "capabilities", info->capabilities },
                { "role", info->role },
                // Clients chunk bulk calls by this limit instead of guessing it.
                { "limits", { { "maxLinesPerRequest", options_.maxLinesPerRequest } } },
            } );
            response.clientId = info->id;
            return response;
        }

        if ( method == "client.list" ) {
            json clients = json::array();
            for ( const auto& info : registry_.list() ) {
                clients.push_back( {
                    { "clientId", info.id },
                    { "name", info.name },
                    { "role", info.role },
                    { "capabilities", info.capabilities },
                    { "connected", true },
                    { "lastActiveMs", info.lastActiveMs },
                } );
            }
            return ok( { { "clients", std::move( clients ) } } );
        }

        if ( method == "recorder.config" ) {
            if ( !canUseRecorder( clientId ) ) {
                return fail( 403, "Forbidden", "recorder capability required" );
            }
            json commands = json::array();
            for ( const auto& command : recorderConfig_.commands() ) {
                commands.push_back( { { "name", command.name }, { "command", command.command } } );
            }
            json bindings = json::array();
            for ( const auto& [file, command] : recorderConfig_.bindings() ) {
                bindings.push_back( { { "file", file }, { "command", command } } );
            }
            return ok( { { "commands", std::move( commands ) },
                         { "bindings", std::move( bindings ) } } );
        }

        if ( method == "recorder.commands.set" ) {
            if ( !canUseRecorder( clientId ) ) {
                return fail( 403, "Forbidden", "recorder capability required" );
            }
            const auto input = params.value( "commands", json::array() );
            if ( !input.is_array() ) {
                return fail( 400, "InvalidRequest", "commands must be an array" );
            }
            std::vector<RecorderCommandConfig> commands;
            for ( const auto& item : input ) {
                if ( !item.is_object() ) {
                    continue;
                }
                commands.push_back(
                    { item.value( "name", std::string{} ),
                      item.value( "command", std::string{} ) } );
            }
            std::string error;
            if ( !recorderConfig_.replaceCommands( commands, error ) ) {
                return fail( 500, "RecorderConfigFailed", error );
            }
            return ok( json::object() );
        }

        if ( method == "recorder.binding.set" ) {
            if ( !canUseRecorder( clientId ) ) {
                return fail( 403, "Forbidden", "recorder capability required" );
            }
            const auto filePath = params.value( "filePath", std::string{} );
            const auto commandName = params.value( "commandName", std::string{} );
            std::string error;
            if ( !recorderConfig_.setBinding( filePath, commandName, error ) ) {
                return fail( 400, "RecorderBindingFailed", error );
            }
            return ok( json::object() );
        }

        if ( method == "recorder.start" ) {
            if ( !canUseRecorder( clientId ) ) {
                return fail( 403, "Forbidden", "recorder capability required" );
            }
            const auto commandName = params.value( "commandName", std::string{} );
            const auto filePath = params.value( "filePath", std::string{} );
            const auto command = recorderConfig_.commandForName( commandName );
            if ( !command ) {
                return fail( 404, "RecorderCommandNotFound",
                             "recorder command not found: " + commandName );
            }
            std::string error;
            const auto started = recorder_.start( *command, filePath, clientId, error );
            if ( !started ) {
                return fail( 400, "RecorderStartFailed", error );
            }
            return ok( {
                { "processId", started->id },
                { "pid", started->pid },
                { "state", started->state },
            } );
        }

        if ( method == "recorder.status" ) {
            if ( !canUseRecorder( clientId ) ) {
                return fail( 403, "Forbidden", "recorder capability required" );
            }
            const auto processId = params.at( "processId" ).get<std::uint64_t>();
            const auto status = recorder_.status( processId );
            if ( !status ) {
                return fail( 404, "RecorderNotFound", "recorder process not found" );
            }
            return ok( {
                { "processId", status->id },
                { "state", status->state },
                { "exitCode", status->exitCode },
                { "pid", status->pid },
                { "error", status->error },
            } );
        }

        if ( method == "recorder.stop" ) {
            if ( !canUseRecorder( clientId ) ) {
                return fail( 403, "Forbidden", "recorder capability required" );
            }
            const auto processId = params.at( "processId" ).get<std::uint64_t>();
            std::string error;
            if ( !recorder_.stop( processId, error ) ) {
                return fail( 404, "RecorderNotFound", error );
            }
            return ok( json::object() );
        }

        if ( method == "fs.list" ) {
            std::filesystem::path requested;
            if ( params.contains( "path" ) && !params[ "path" ].is_null() ) {
                requested = platform::paths::pathFromUtf8( params[ "path" ].get<std::string>() );
            }
            else if ( !options_.root.empty() ) {
                requested = options_.root;
            }
            else {
                requested = std::filesystem::current_path();
            }
            if ( requested.empty() ) {
                requested = std::filesystem::current_path();
            }

            if ( !pathAllowed( toUtf8( requested ) ) ) {
                return fail( 403, "Forbidden", "path is outside the configured root" );
            }

            std::error_code error;
            auto path = std::filesystem::weakly_canonical( requested, error );
            if ( error ) {
                path = std::filesystem::absolute( requested, error ).lexically_normal();
            }
            if ( error ) {
                return fail( 404, "FileNotFound", "cannot resolve path" );
            }
            if ( std::filesystem::is_regular_file( path, error ) ) {
                path = path.parent_path();
            }
            if ( error || !std::filesystem::is_directory( path, error ) ) {
                return fail( 404, "FileNotFound", "directory does not exist" );
            }

            json roots = json::array();
#ifdef _WIN32
            for ( char letter = 'A'; letter <= 'Z'; ++letter ) {
                std::string drive;
                drive += letter;
                drive += ":\\";
                std::error_code driveError;
                if ( std::filesystem::exists( drive, driveError ) ) {
                    roots.push_back( { { "name", drive }, { "path", drive } } );
                }
            }
#else
            roots.push_back( { { "name", "/" }, { "path", "/" } } );
            if ( const auto* home = std::getenv( "HOME" ); home != nullptr ) {
                roots.push_back( { { "name", home }, { "path", home } } );
            }
#endif

            struct Entry {
                std::string name;
                std::string path;
                bool directory = false;
                int64_t sizeBytes = 0;
            };

            std::vector<Entry> entries;
            constexpr std::size_t kMaxEntries = 2000;
            bool truncated = false;

            std::error_code iteratorError;
            std::filesystem::directory_iterator iterator(
                path, std::filesystem::directory_options::skip_permission_denied, iteratorError );
            if ( iteratorError ) {
                return fail( 403, "Forbidden", "cannot list directory" );
            }
            for ( const auto end = std::filesystem::directory_iterator(); iterator != end;
                  iterator.increment( iteratorError ) ) {
                if ( iteratorError ) {
                    break;
                }
                if ( entries.size() >= kMaxEntries ) {
                    truncated = true;
                    break;
                }

                std::error_code entryError;
                const auto& entryPath = iterator->path();
                const bool directory = iterator->is_directory( entryError );
                if ( entryError ) {
                    continue;
                }
                const bool regularFile = iterator->is_regular_file( entryError );
                if ( entryError || ( !directory && !regularFile ) ) {
                    continue;
                }

                auto size = int64_t{ 0 };
                if ( regularFile ) {
                    const auto fileSize = iterator->file_size( entryError );
                    if ( !entryError ) {
                        size = static_cast<int64_t>( fileSize );
                    }
                }

                entries.push_back(
                    { toUtf8( entryPath.filename() ), toUtf8( entryPath ), directory, size } );
            }

            std::sort( entries.begin(), entries.end(), []( const Entry& left, const Entry& right ) {
                if ( left.directory != right.directory ) {
                    return left.directory;
                }
                return lowerAscii( left.name ) < lowerAscii( right.name );
            } );

            json entriesJson = json::array();
            for ( const auto& entry : entries ) {
                entriesJson.push_back( {
                    { "name", entry.name },
                    { "path", entry.path },
                    { "directory", entry.directory },
                    { "sizeBytes", entry.sizeBytes },
                } );
            }

            auto parent = path.parent_path();
            if ( !parent.empty() && !pathAllowed( toUtf8( parent ) ) ) {
                parent.clear();
            }

            return ok( {
                { "path", toUtf8( path ) },
                { "parent", parent.empty() ? json( nullptr ) : json( toUtf8( parent ) ) },
                { "roots", std::move( roots ) },
                { "entries", std::move( entriesJson ) },
                { "truncated", truncated },
            } );
        }

        if ( method == "file.openUrl" ) {
            const auto url = params.at( "url" ).get<std::string>();
            if ( url.rfind( "https://", 0 ) == 0 ) {
                return fail( 400, "UrlNotSupported",
                             "HTTPS is not enabled in this server build; use http:// or "
                             "download the file locally" );
            }
            const auto parsed = parseHttpUrl( url );
            if ( !parsed ) {
                return fail( 400, "InvalidUrl", "only http:// URLs are supported" );
            }

            std::error_code error;
            auto cacheRoot = options_.root.empty()
                                 ? std::filesystem::temp_directory_path( error )
                                 : options_.root;
            if ( error ) {
                return fail( 500, "InternalError", "cannot resolve temporary directory" );
            }
            cacheRoot /= ".klogg-url-cache";
            std::filesystem::create_directories( cacheRoot, error );
            if ( error ) {
                return fail( 500, "InternalError", "cannot create download directory" );
            }

            const auto stamp = static_cast<uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count() );
            const auto downloadDir = cacheRoot / std::to_string( stamp );
            std::filesystem::create_directories( downloadDir, error );
            if ( error ) {
                return fail( 500, "InternalError", "cannot create download directory" );
            }

            const auto targetPath
                = downloadDir / platform::paths::pathFromUtf8( downloadFileName( url ) );

            std::ofstream output( targetPath, std::ios::binary );
            if ( !output ) {
                return fail( 500, "InternalError", "cannot create download file" );
            }

            constexpr std::size_t maxDownloadBytes = 1024ull * 1024ull * 1024ull;
            std::size_t written = 0;
            bool tooLarge = false;
            bool statusOk = false;

            try {
                httplib::Client client( parsed->host, parsed->port );
                client.set_connection_timeout( 10, 0 );
                client.set_read_timeout( 60, 0 );
                client.set_follow_location( true );
                client.Get(
                    parsed->target,
                    [ & ]( const httplib::Response& response ) {
                        statusOk = response.status >= 200 && response.status < 300;
                        if ( !statusOk ) {
                            return false;
                        }
                        const auto contentLength
                            = response.get_header_value_u64( "Content-Length", 0 );
                        if ( contentLength > maxDownloadBytes ) {
                            tooLarge = true;
                            return false;
                        }
                        return true;
                    },
                    [ & ]( const char* data, std::size_t length ) {
                        if ( written + length > maxDownloadBytes ) {
                            tooLarge = true;
                            return false;
                        }
                        output.write( data, static_cast<std::streamsize>( length ) );
                        if ( !output ) {
                            return false;
                        }
                        written += length;
                        return true;
                    } );
            }
            catch ( const std::exception& exception ) {
                output.close();
                std::filesystem::remove( targetPath, error );
                return fail( 400, "UrlError", exception.what() );
            }

            output.close();
            if ( tooLarge ) {
                std::filesystem::remove( targetPath, error );
                return fail( 400, "UrlTooLarge", "download exceeds 1 GiB" );
            }
            if ( !output ) {
                std::filesystem::remove( targetPath, error );
                return fail( 500, "InternalError", "cannot write downloaded file" );
            }
            if ( !statusOk || written == 0 ) {
                std::filesystem::remove( targetPath, error );
                return fail( 400, "UrlError", "download failed or returned an empty body" );
            }

            // The downloaded payload may be a compressed log (.gz/.bz2/.xz) or
            // an archive; the desktop version extracts those before loading.
            auto currentPath = targetPath;
            for ( int depth = 0; depth < 3; ++depth ) {
                const auto format = compression::detectArchiveFormat( currentPath );
                if ( format == compression::ArchiveFormat::None ) {
                    break;
                }

                if ( compression::isStreamCompressed( format ) ) {
                    const auto name = currentPath.filename().u8string();
                    const auto decompressed
                        = currentPath.parent_path()
                          / platform::paths::pathFromUtf8(
                              compression::decompressedFileName( std::string(
                                  reinterpret_cast<const char*>( name.data() ), name.size() ) ) );
                    const auto decompression
                        = compression::decompressStream( format, currentPath, decompressed );
                    if ( !decompression.ok ) {
                        return fail( 400, "DecompressError", decompression.message );
                    }
                    currentPath = decompressed;
                    continue;
                }

                const auto extractedDir = currentPath.parent_path() / "extracted";
                const auto extraction = compression::extractArchive( format, currentPath,
                                                                     extractedDir );
                if ( !extraction.ok ) {
                    return fail( 400, "ExtractError", extraction.message );
                }
                if ( extraction.files.size() == 1 ) {
                    currentPath = extraction.files.front().path;
                    break;
                }

                auto files = json::array();
                for ( const auto& file : extraction.files ) {
                    const auto relative = file.path.lexically_relative( extractedDir );
                    files.push_back( { { "name", toUtf8( relative ) },
                                       { "path", toUtf8( file.path ) },
                                       { "size", file.size } } );
                }
                return ok( { { "archive", { { "files", files } } } } );
            }

            const auto result = session_.openFileSync( toUtf8( currentPath ), std::nullopt, false );
            if ( !result ) {
                return fail( result.error() );
            }
            registry_.addFile( clientId, result->id );
            return ok( toJson( *result ) );
        }

        // Clipboard text is log content, not a path: materialise it so the
        // normal open path can display it as a temporary log file.
        if ( method == "file.saveTemp" ) {
            const auto text = params.value( "text", std::string{} );
            if ( text.empty() ) {
                return fail( 400, "InvalidArgument", "text is empty" );
            }

            std::error_code error;
            auto cacheDir = options_.root.empty()
                                ? std::filesystem::temp_directory_path( error )
                                : options_.root;
            if ( error ) {
                return fail( 500, "InternalError", "cannot resolve temporary directory" );
            }
            // Keep the snapshot inside the configured root so file.open accepts it.
            cacheDir /= ".klogg-clipboard-cache";
            std::filesystem::create_directories( cacheDir, error );
            if ( error ) {
                return fail( 500, "InternalError", "cannot create clipboard cache directory: " + error.message() + " (" + toUtf8( cacheDir ) + ")" );
            }

            const auto stamp = static_cast<uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count() );
            const auto targetPath
                = cacheDir / ( "clipboard-" + std::to_string( stamp ) + ".log" );

            std::ofstream output( targetPath, std::ios::binary | std::ios::trunc );
            if ( !output ) {
                return fail( 500, "InternalError", "cannot create clipboard file" );
            }
            output.write( text.data(), static_cast<std::streamsize>( text.size() ) );
            output.close();
            if ( !output ) {
                return fail( 500, "InternalError", "cannot write clipboard file" );
            }

            return ok( { { "path", toUtf8( targetPath ) } } );
        }

        if ( method == "file.open" ) {
            const auto path = params.at( "path" ).get<std::string>();
            if ( !pathAllowed( path ) ) {
                return fail( 403, "Forbidden", "path is outside the configured root" );
            }
            std::optional<std::string> encoding;
            if ( params.contains( "encoding" ) && !params[ "encoding" ].is_null() ) {
                encoding = params[ "encoding" ].get<std::string>();
            }
            const auto result
                = session_.openFileSync( path, encoding, params.value( "allowMissing", false ) );
            if ( !result ) {
                return fail( result.error() );
            }
            registry_.addFile( clientId, result->id );
            return ok( toJson( *result ) );
        }

        if ( method == "file.close" ) {
            const auto id = fileId( params );
            if ( !clientId.empty() && !registry_.ownsFile( clientId, id ) ) {
                return fail( 403, "Forbidden", "file was not opened by this client" );
            }
            const auto result = session_.closeFile( id );
            if ( result ) {
                registry_.removeFile( clientId, id );
            }
            return result ? ok( json::object() ) : fail( result.error() );
        }

        if ( method == "file.info" ) {
            const auto result = session_.fileInfo( fileId( params ) );
            return result ? ok( toJson( *result ) ) : fail( result.error() );
        }

        if ( method == "file.list" ) {
            const auto result = session_.openFiles();
            if ( !result ) {
                return fail( result.error() );
            }
            json files = json::array();
            for ( const auto& info : *result ) {
                files.push_back( toJson( info ) );
            }
            return ok( { { "files", std::move( files ) } } );
        }

        if ( method == "file.stat" ) {
            const auto info = session_.fileInfo( fileId( params ) );
            if ( !info ) {
                return fail( info.error() );
            }
            std::error_code error;
            const auto size
                = std::filesystem::file_size( platform::paths::pathFromUtf8( info->path ), error );
            return ok( {
                { "fileId", info->id },
                { "sizeBytes", error ? info->size_bytes : static_cast<int64_t>( size ) },
                { "totalLines", info->total_lines.get() },
                { "lastModifiedMs", info->last_modified_ms },
            } );
        }

        if ( method == "file.reload" ) {
            const auto id = fileId( params );
            if ( !clientId.empty() && !registry_.ownsFile( clientId, id ) ) {
                return fail( 403, "Forbidden", "file was not opened by this client" );
            }
            const auto result = session_.reloadFile( id );
            return result ? ok( toJson( *result ) ) : fail( result.error() );
        }

        if ( method == "file.interruptLoading" ) {
            const auto id = fileId( params );
            if ( !clientId.empty() && !registry_.ownsFile( clientId, id ) ) {
                return fail( 403, "Forbidden", "file was not opened by this client" );
            }
            const auto result = session_.interruptLoading( id );
            return result ? ok( json::object() ) : fail( result.error() );
        }

        if ( method == "file.watch" ) {
            const auto id = fileId( params );
            if ( !clientId.empty() && !registry_.ownsFile( clientId, id ) ) {
                return fail( 403, "Forbidden", "file was not opened by this client" );
            }
            const auto result = session_.watchFile( id, params.value( "enabled", true ) );
            return result ? ok( json::object() ) : fail( result.error() );
        }

        if ( method == "file.checkChanges" ) {
            const auto id = fileId( params );
            if ( !clientId.empty() && !registry_.ownsFile( clientId, id ) ) {
                return fail( 403, "Forbidden", "file was not opened by this client" );
            }
            const auto result = session_.checkFileChanges( id );
            return result ? ok( json::object() ) : fail( result.error() );
        }

        if ( method == "file.setEncoding" ) {
            const auto id = fileId( params );
            if ( !clientId.empty() && !registry_.ownsFile( clientId, id ) ) {
                return fail( 403, "Forbidden", "file was not opened by this client" );
            }
            const auto result
                = session_.setEncoding( id, params.at( "encoding" ).get<std::string>() );
            return result ? ok( json::object() ) : fail( result.error() );
        }

        if ( method == "file.supportedEncodings" ) {
            const auto result = session_.supportedEncodings();
            return result ? ok( { { "encodings", *result } } ) : fail( result.error() );
        }

        if ( method == "highlighters.set" ) {
            std::vector<api::HighlightRule> rules;
            for ( const auto& item : params.at( "rules" ) ) {
                try {
                    rules.push_back( parseHighlightRule( item ) );
                }
                catch ( const std::exception& error ) {
                    return fail( 400, "InvalidArgument", error.what() );
                }
            }
            const auto result = session_.setHighlighters( rules );
            return result ? ok( json::object() ) : fail( result.error() );
        }

        if ( method == "quickHighlighters.set" ) {
            std::vector<api::QuickHighlighterDef> defs;
            for ( const auto& item : params.at( "defs" ) ) {
                try {
                    defs.push_back( parseQuickHighlighterDef( item ) );
                }
                catch ( const std::exception& error ) {
                    return fail( 400, "InvalidArgument", error.what() );
                }
            }
            const auto result = session_.setQuickHighlighters( defs );
            return result ? ok( json::object() ) : fail( result.error() );
        }

        if ( method == "lines.get" ) {
            const auto count = clampedCount( params, options_.maxLinesPerRequest );
            const auto result = session_.getLines(
                fileId( params ), LineNumber( params.at( "from" ).get<uint64_t>() ),
                LinesCount( count ), parseAnsiMode( params ) );
            return result ? ok( { { "lines", linesToJson( *result ) } } ) : fail( result.error() );
        }

        if ( method == "lines.getBatch" ) {
            const auto result = session_.getLinesBatch(
                fileId( params ), requestedLines( params, options_.maxLinesPerRequest ),
                parseAnsiMode( params ) );
            return result ? ok( { { "lines", linesToJson( *result ) } } ) : fail( result.error() );
        }

        if ( method == "render.viewport" ) {
            const auto visibleLines = std::min<uint64_t>(
                params.value( "visibleLines", uint64_t{ 0 } ), options_.maxLinesPerRequest );
            const auto result = session_.renderViewport(
                fileId( params ), optionalSearchId( params ),
                LineNumber( params.value( "firstLine", uint64_t{ 0 } ) ),
                LinesCount( visibleLines ), parseViewportOptions( params ) );
            return result ? ok( toJson( *result ) ) : fail( result.error() );
        }

        if ( method == "render.linesHtml" ) {
            const auto lines = requestedLines( params, options_.maxLinesPerRequest );
            const auto truncated = params.at( "lines" ).size() > lines.size();
            const auto result = session_.renderLinesHtml( fileId( params ), optionalSearchId( params ),
                                                          lines, parseHtmlRenderOptions( params ) );
            if ( !result ) {
                return fail( result.error() );
            }
            auto payload = toJson( *result );
            payload[ "truncated" ] = truncated;
            return ok( std::move( payload ) );
        }

        if ( method == "search.validatePattern" ) {
            const auto result = session_.validatePattern( params.at( "pattern" ).get<std::string>(),
                                                          params.value( "isRegex", true ) );
            return result ? ok( toJson( *result ) ) : fail( result.error() );
        }

        if ( method == "search.run" ) {
            const auto result = session_.runSearch( fileId( params ), parseSearchParams( params ) );
            if ( !result ) {
                return fail( result.error() );
            }
            registry_.addSearch( clientId, *result );
            return ok( { { "searchId", *result } } );
        }

        if ( method == "search.runBoolean" ) {
            const auto result = session_.runBooleanSearch(
                fileId( params ), params.at( "expression" ).get<std::string>() );
            if ( !result ) {
                return fail( result.error() );
            }
            registry_.addSearch( clientId, *result );
            return ok( { { "searchId", *result } } );
        }

        if ( method == "search.progress" ) {
            const auto result = session_.searchProgress( searchId( params ) );
            return result ? ok( toJson( *result ) ) : fail( result.error() );
        }

        if ( method == "search.extend" ) {
            const auto id = searchId( params );
            if ( !clientId.empty() && !registry_.ownsSearch( clientId, id ) ) {
                return fail( 403, "Forbidden", "search belongs to another client" );
            }
            const auto result = session_.extendSearch(
                id, LineNumber( params.at( "toLine" ).get<uint64_t>() ) );
            return result ? ok( json::object() ) : fail( result.error() );
        }

        if ( method == "search.matches" ) {
            const auto count = clampedCount( params, options_.maxLinesPerRequest );
            const auto result
                = session_.searchMatches( searchId( params ), params.value( "from", uint64_t{ 0 } ),
                                          static_cast<uint32_t>( std::min<uint64_t>(
                                              count, std::numeric_limits<uint32_t>::max() ) ) );
            return result ? ok( { { "matches", lineNumbersToJson( *result ) } } )
                          : fail( result.error() );
        }

        if ( method == "search.matchIndex" ) {
            const auto result = session_.searchMatchIndex(
                searchId( params ), LineNumber( params.at( "line" ).get<uint64_t>() ) );
            return result ? ok( optionalIndexToJson( *result ) ) : fail( result.error() );
        }

        if ( method == "search.filteredLines" ) {
            const auto count = clampedCount( params, options_.maxLinesPerRequest );
            const auto result = session_.getFilteredLines(
                searchId( params ), params.value( "fromIndex", uint64_t{ 0 } ),
                static_cast<uint32_t>(
                    std::min<uint64_t>( count, std::numeric_limits<uint32_t>::max() ) ),
                parseAnsiMode( params ) );
            return result ? ok( { { "lines", linesToJson( *result ) } } ) : fail( result.error() );
        }

        if ( method == "search.filteredLineNumbers" ) {
            const auto count = clampedCount( params, options_.maxLinesPerRequest );
            const auto result = session_.filteredLineNumbers(
                searchId( params ), params.value( "fromIndex", uint64_t{ 0 } ),
                static_cast<uint32_t>(
                    std::min<uint64_t>( count, std::numeric_limits<uint32_t>::max() ) ) );
            return result ? ok( { { "lines", lineNumbersToJson( *result ) } } )
                          : fail( result.error() );
        }

        if ( method == "search.setVisibility" ) {
            const auto id = searchId( params );
            if ( !clientId.empty() && !registry_.ownsSearch( clientId, id ) ) {
                return fail( 403, "Forbidden", "search belongs to another client" );
            }
            const auto result = session_.setSearchVisibility( id, params.value( "matches", true ),
                                                              params.value( "marks", false ),
                                                              params.value( "events", false ) );
            return result ? ok( { { "lineCount", result->get() } } ) : fail( result.error() );
        }

        if ( method == "search.filteredLineCount" ) {
            const auto result = session_.getFilteredLineCount( searchId( params ) );
            return result ? ok( { { "lineCount", result->get() } } ) : fail( result.error() );
        }

        if ( method == "search.filteredLineIndex" ) {
            const auto result = session_.filteredLineIndex(
                searchId( params ), LineNumber( params.at( "line" ).get<uint64_t>() ) );
            return result ? ok( optionalIndexToJson( *result ) ) : fail( result.error() );
        }

        if ( method == "search.filteredLineIndexAtOrBefore" ) {
            const auto result = session_.filteredLineIndexAtOrBefore(
                searchId( params ), LineNumber( params.at( "line" ).get<uint64_t>() ) );
            return result ? ok( optionalIndexToJson( *result ) ) : fail( result.error() );
        }

        if ( method == "search.filteredMaxLineLength" ) {
            const auto result = session_.filteredMaxLineLength( searchId( params ) );
            return result ? ok( { { "maxLineLength", result->get() } } ) : fail( result.error() );
        }

        if ( method == "search.cancel" ) {
            const auto id = searchId( params );
            if ( !clientId.empty() && !registry_.ownsSearch( clientId, id ) ) {
                return fail( 403, "Forbidden", "search belongs to another client" );
            }
            const auto result = session_.cancelSearch( id );
            if ( result ) {
                registry_.removeSearch( clientId, id );
            }
            return result ? ok( json::object() ) : fail( result.error() );
        }

        if ( method == "search.interrupt" ) {
            const auto id = searchId( params );
            if ( !clientId.empty() && !registry_.ownsSearch( clientId, id ) ) {
                return fail( 403, "Forbidden", "search belongs to another client" );
            }
            const auto result = session_.interruptSearch( id );
            return result ? ok( json::object() ) : fail( result.error() );
        }

        if ( method == "marks.list" ) {
            const auto result = session_.listMarks( fileId( params ) );
            return result ? ok( { { "marks", lineNumbersToJson( *result ) } } )
                          : fail( result.error() );
        }

        if ( method == "marks.add" ) {
            const auto lines = requestedLines( params, options_.maxLinesPerRequest );
            const auto truncated = params.at( "lines" ).size() > lines.size();
            const auto result = session_.addMarks( fileId( params ), lines );
            return result ? ok( { { "truncated", truncated } } ) : fail( result.error() );
        }

        if ( method == "marks.remove" ) {
            const auto lines = requestedLines( params, options_.maxLinesPerRequest );
            const auto truncated = params.at( "lines" ).size() > lines.size();
            const auto result = session_.removeMarks( fileId( params ), lines );
            return result ? ok( { { "truncated", truncated } } ) : fail( result.error() );
        }

        if ( method == "marks.after" ) {
            const auto result = session_.markAfter(
                fileId( params ), LineNumber( params.at( "line" ).get<uint64_t>() ) );
            return result ? ok( optionalLineToJson( *result ) ) : fail( result.error() );
        }

        if ( method == "marks.before" ) {
            const auto result = session_.markBefore(
                fileId( params ), LineNumber( params.at( "line" ).get<uint64_t>() ) );
            return result ? ok( optionalLineToJson( *result ) ) : fail( result.error() );
        }

        if ( method == "flags.get" ) {
            const auto count = clampedCount( params, options_.maxLinesPerRequest );
            const auto result = session_.lineFlags(
                fileId( params ), optionalSearchId( params ),
                LineNumber( params.value( "from", uint64_t{ 0 } ) ), LinesCount( count ) );
            return result ? ok( { { "flags", flagsToJson( *result ) } } ) : fail( result.error() );
        }

        if ( method == "flags.getBatch" ) {
            const auto result
                = session_.lineFlagsBatch( fileId( params ), optionalSearchId( params ),
                                           requestedLines( params, options_.maxLinesPerRequest ) );
            return result ? ok( { { "flags", flagsToJson( *result ) } } ) : fail( result.error() );
        }

        return fail( 404, "MethodNotFound", "unknown method: " + method );
    } catch ( const std::exception& error ) {
        return fail( 400, "InvalidRequest", error.what() );
    }
}

} // namespace klogg::server
