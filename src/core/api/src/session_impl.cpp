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

#include "session_impl.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

#include "ansi_parser.h"
#include "encoding.h"
#include "filewatcher.h"
#include "log.h"
#include "paths.h"
#include "pcre2regex.h"

#include "event_mapping.h"
#include "tracing.h"

namespace klogg::api {

// C4-5 read channel switch (see session_impl.h)
bool readChannelQueued()
{
    static const bool queued = [] {
        const char* value = std::getenv( "KLOGG_READ_CHANNEL" );
        return value != nullptr && std::strcmp( value, "queue" ) == 0;
    }();
    return queued;
}

namespace {

// Generous bound so a synchronous open can never hang forever
constexpr int kIndexingTimeoutMs = 10 * 60 * 1000;

// Deleter-post scheme (C4-5): with reads running on the caller's thread, the
// last reference to an engine object may drop on any thread, but destruction
// must still run on the executor (LogData teardown shuts down its operation
// queue and unregisters the file watcher observer). The executor is shared so
// a late release can never dereference a dead one; posting to a stopped
// executor drops the task (same as the old queued connections after the
// thread quit): a bounded one-time leak at session teardown.
template <typename T> void deleteOnExecutor( const std::shared_ptr<SessionExecutor>& executor, T* p )
{
    executor->post( [ p ] { delete p; } );
}

// The filtered data's deleter also holds the owning LogData: the posted
// deletion frees the filtered data first and only then drops the LogData
// reference, so the engine constraint "every LogFilteredData dies before its
// LogData" holds no matter where the last reference is released (the executor
// queue is FIFO, the LogData deletion is posted after).
std::shared_ptr<LogFilteredData>
wrapFilteredData( std::unique_ptr<LogFilteredData> filtered,
                  const std::shared_ptr<SessionExecutor>& executor,
                  const std::shared_ptr<LogData>& logData )
{
    return std::shared_ptr<LogFilteredData>(
        filtered.release(), [ executor, keepAlive = logData ]( LogFilteredData* p ) mutable {
            executor->post( [ p, keepAlive = std::move( keepAlive ) ]() mutable {
                delete p;
                keepAlive.reset();
            } );
        } );
}

// Turns the colour label words of one frame into the rules the composition
// takes: the words come from the consumer, the colours from the session state,
// and slots the session does not have are dropped (the paint path used to stop
// at the same place).
std::vector<klogg::render::QuickHighlighterOptions>
quickHighlighterSlots( const std::shared_ptr<const EngineContext::PaintHighlightState>& highlightState,
                       const std::vector<std::vector<std::string>>& words )
{
    std::vector<klogg::render::QuickHighlighterOptions> slots;
    if ( !highlightState ) {
        return slots;
    }

    const auto& colors = highlightState->quickHighlighters;
    if ( words.size() > colors.size() ) {
        LOG_WARNING << "Not enough quickHighlighters configured";
    }

    const auto slotCount = std::min( words.size(), colors.size() );
    slots.reserve( slotCount );
    for ( size_t i = 0; i < slotCount; ++i ) {
        klogg::render::QuickHighlighterOptions slot;
        slot.words = words[ i ];
        slot.foreColor = colors[ i ].color.foreColor;
        slot.backColor = colors[ i ].color.backColor;
        slots.push_back( std::move( slot ) );
    }
    return slots;
}

// A window wide enough for any line. layoutLine measures the trailing plain run
// from the visible range, so a copy needs a range no line can outgrow; the
// column arithmetic stays far below the overflow limit of firstColumn +
// visibleColumns.
constexpr LineLength kFullLineWindow
    = LineLength{ std::numeric_limits<LineLength::UnderlyingType>::max() / 2 };

constexpr std::string_view kHtmlPreOpen
    = "<pre style=\"font-family:monospace; white-space:pre; margin:0\">";

// The Qt copy wrote plain "#rrggbb" while Rgba::toHex keeps the API's
// alpha-first "#aarrggbb" convention, so the clipboard serialization needs its
// own form.
std::string rgbHexString( const Rgba& color )
{
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string hex = "#000000";
    const uint8_t channels[] = { color.r, color.g, color.b };
    for ( std::size_t i = 0; i < 3; ++i ) {
        hex[ 1 + 2 * i ] = kHexDigits[ channels[ i ] >> 4 ];
        hex[ 2 + 2 * i ] = kHexDigits[ channels[ i ] & 0xf ];
    }
    return hex;
}

// Escapes the four characters the Qt copy escaped. Operating on UTF-8 bytes is
// safe: none of them occurs inside a multi-byte sequence.
std::string htmlEscape( std::string_view text )
{
    std::string escaped;
    escaped.reserve( text.size() );
    for ( const auto character : text ) {
        switch ( character ) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

// One character of the display text with its UTF-8 byte range and its UTF-16
// code unit range. The chunks are measured in UTF-16 code units while the text
// is UTF-8; a malformed byte counts as one column and is copied through as it
// is, the same per-byte column model the wrapper falls back to.
struct DisplayCharacter {
    std::size_t beginByte = 0;
    std::size_t endByte = 0;
    std::size_t column = 0;
    std::size_t columns = 1;
};

std::vector<DisplayCharacter> splitDisplayCharacters( std::string_view text )
{
    std::vector<DisplayCharacter> characters;
    characters.reserve( text.size() );

    std::size_t byte = 0;
    std::size_t column = 0;
    while ( byte < text.size() ) {
        const auto lead = static_cast<unsigned char>( text[ byte ] );
        std::size_t sequenceLength = 1;
        if ( lead >= 0x80 ) {
            if ( ( lead & 0xe0 ) == 0xc0 && lead >= 0xc2 ) {
                sequenceLength = 2;
            }
            else if ( ( lead & 0xf0 ) == 0xe0 ) {
                sequenceLength = 3;
            }
            else if ( ( lead & 0xf8 ) == 0xf0 && lead <= 0xf4 ) {
                sequenceLength = 4;
            }
        }

        auto valid = sequenceLength > 1 && byte + sequenceLength <= text.size();
        for ( std::size_t i = 1; valid && i < sequenceLength; ++i ) {
            valid = ( static_cast<unsigned char>( text[ byte + i ] ) & 0xc0 ) == 0x80;
        }
        if ( !valid ) {
            sequenceLength = 1;
        }

        // A four byte sequence is a surrogate pair, so it spans two columns
        const auto columns = sequenceLength == 4 ? std::size_t{ 2 } : std::size_t{ 1 };
        characters.push_back( DisplayCharacter{ byte, byte + sequenceLength, column, columns } );
        byte += sequenceLength;
        column += columns;
    }

    return characters;
}

struct LineRunColor {
    Rgba fore;
    Rgba back;
    bool highlighted = false;

    bool operator==( const LineRunColor& ) const = default;
};

// Colours of every UTF-16 column of a line, applied in chunk order: a later
// chunk overwrites an earlier one, which only matters defensively because the
// layout runs do not overlap. A chunk carrying the row colours is plain text
// and must not become a span; columns no chunk covers stay plain as well.
std::vector<LineRunColor> lineColumnColors( const klogg::render::FrameLine& line,
                                            std::size_t columnCount )
{
    std::vector<LineRunColor> colors( columnCount );
    for ( const auto& chunk : line.chunks ) {
        if ( chunk.length.get() <= 0 ) {
            continue;
        }

        const LineRunColor color{ chunk.foreColor, chunk.backColor,
                                  chunk.foreColor != line.foreColor
                                      || chunk.backColor != line.backColor };
        const auto chunkStart = std::max<LineColumn::UnderlyingType>( chunk.start.get(), 0 );
        const auto chunkEnd = chunk.start.get() + chunk.length.get();
        for ( auto column = static_cast<std::size_t>( chunkStart );
              column < columnCount && static_cast<LineColumn::UnderlyingType>( column ) < chunkEnd;
              ++column ) {
            colors[ column ] = color;
        }
    }
    return colors;
}

std::string htmlLine( const klogg::render::FrameLine& line )
{
    const auto characters = splitDisplayCharacters( line.displayText );
    if ( characters.empty() ) {
        return {};
    }

    const auto columnCount = characters.back().column + characters.back().columns;
    const auto colors = lineColumnColors( line, columnCount );

    std::string html;
    for ( std::size_t first = 0; first < characters.size(); ) {
        const auto color = colors[ characters[ first ].column ];
        auto last = first + 1;
        while ( last < characters.size() && colors[ characters[ last ].column ] == color ) {
            ++last;
        }

        const auto beginByte = characters[ first ].beginByte;
        const auto endByte = characters[ last - 1 ].endByte;
        const auto text = htmlEscape(
            std::string_view( line.displayText ).substr( beginByte, endByte - beginByte ) );

        if ( !color.highlighted ) {
            html += text;
        }
        else {
            html += "<span style=\"color:";
            html += rgbHexString( color.fore );
            // A transparent background, or the row background the layout
            // substituted for it, means the match set no background; Qt leaves
            // the background out for those matches too
            if ( color.back.a != 0 && color.back != line.backColor ) {
                html += ";background-color:";
                html += rgbHexString( color.back );
            }
            html += "\">";
            html += text;
            html += "</span>";
        }

        first = last;
    }
    return html;
}

RenderedHtml serializeFrameAsHtml( const klogg::render::Frame& frame )
{
    RenderedHtml result;
    result.html += kHtmlPreOpen;

    for ( std::size_t i = 0; i < frame.lines.size(); ++i ) {
        if ( i > 0 ) {
            result.html += '\n';
            result.text += '\n';
        }
        result.html += htmlLine( frame.lines[ i ] );
        result.text += frame.lines[ i ].displayText;
    }

    result.html += "</pre>";
    return result;
}

// Runs the callable when the scope exits, whatever the exit path: a pending
// registration that holds a pointer to a stack local must not survive a throw
template <typename Fn> class ScopeExitGuard {
public:
    explicit ScopeExitGuard( Fn fn )
        : fn_( std::move( fn ) )
    {
    }

    ScopeExitGuard( const ScopeExitGuard& ) = delete;
    ScopeExitGuard& operator=( const ScopeExitGuard& ) = delete;

    ~ScopeExitGuard()
    {
        fn_();
    }

private:
    Fn fn_;
};

// Waits for one loadingFinished delivery. Must run on the worker thread;
// the pump also services other enqueued calls in the meantime.
std::optional<LoadingStatus> waitForLoadingFinished( LogData* logData,
                                                     SessionExecutor& executor )
{
    std::optional<LoadingStatus> result;
    const auto listenerId = logData->addLoadingFinishedListener(
        [ &result, &executor ]( LoadingStatus status ) {
            result = status;
            // The listener fires on the indexing thread; the dummy post also
            // wakes the executor's pump that is waiting for the result.
            executor.post( [] {} );
        } );
    // The listener captures the stack-local result, so it must be dropped on
    // every exit path: a throw out of the pump would otherwise leave a
    // registration writing through a dangling reference at the next
    // completion
    const ScopeExitGuard unregisterListener(
        [ logData, listenerId ] { logData->removeLoadingFinishedListener( listenerId ); } );
    executor.pumpUntil( [ &result ] { return result.has_value(); },
                        std::chrono::milliseconds( kIndexingTimeoutMs ) );
    return result;
}

// Waits for the completion of a reindex the caller just triggered. An
// interrupt racing the trigger surfaces as a stale Interrupted emission from
// the operation that was in flight; the reindex's own completion arrives
// after it (engine operations complete one emission each, in queue order),
// so an Interrupted is only skipped while the engine still has an operation
// executing or queued. A deliberate interrupt of the reindex itself leaves
// the queue empty and is reported right away instead of blocking until the
// timeout for a completion that never comes.
std::optional<LoadingStatus> waitForReindexCompletion( LogData* logData,
                                                       SessionExecutor& executor )
{
    std::optional<LoadingStatus> status;
    do {
        status = waitForLoadingFinished( logData, executor );
    } while ( status && *status == LoadingStatus::Interrupted && logData->hasOperationInFlight() );
    return status;
}

std::string jsonEscape( const std::string& value )
{
    std::string escaped;
    escaped.reserve( value.size() );
    for ( const char c : value ) {
        switch ( c ) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        default:
            escaped += c;
        }
    }
    return escaped;
}

const char* loadingStatusName( LoadingStatus status )
{
    switch ( status ) {
    case LoadingStatus::Successful:
        return "successful";
    case LoadingStatus::Interrupted:
        return "interrupted";
    case LoadingStatus::NoMemory:
        return "no memory";
    }
    return "unknown";
}

// Engine error kinds are reported as stable strings in the "engine.error"
// payload: an enum would tie every consumer to the engine headers
const char* engineErrorKindName( EngineErrorKind kind )
{
    switch ( kind ) {
    case EngineErrorKind::TooLongLines:
        return "tooLongLines";
    case EngineErrorKind::Exception:
        return "exception";
    }
    return "exception";
}

// Converts one engine line to the API shape, applying the ANSI mode. The
// line is taken by value so the Raw path can move it into the result instead
// of copying the text a second time.
LineData convertLine( std::string line, LineNumber num, AnsiMode ansi )
{
    const klogg::Rgba defaultFg{ 255, 255, 255 };
    const klogg::Rgba defaultBg{ 0, 0, 0 };

    const auto apiColor = []( const klogg::Rgba& color ) {
        const auto hex = color.toHex();
        // API contract is "#rrggbb" (no alpha); toHex yields "#aarrggbb"
        return hex.empty() ? "" : hex.substr( 3 );
    };

    LineData data;
    data.num = num;
    switch ( ansi ) {
    case AnsiMode::Raw:
        data.text = std::move( line );
        break;
    case AnsiMode::Strip:
        data.text
            = AnsiSgrParser::parseLine( line, AnsiProcessing::StripOnly, defaultFg, defaultBg )
                  .cleanText;
        break;
    case AnsiMode::Segments: {
        const auto parsed = AnsiSgrParser::parseLine( line, AnsiProcessing::RenderColors,
                                                      defaultFg, defaultBg );
        data.text = parsed.cleanText;
        data.segments.reserve( parsed.segments.size() );
        for ( const auto& segment : parsed.segments ) {
            klogg::api::AnsiSegment apiSegment;
            apiSegment.start = static_cast<uint32_t>( segment.startColumn );
            apiSegment.end = static_cast<uint32_t>( segment.startColumn + segment.length );
            // The engine parser only reports foreground colors; bg and
            // bold/italic/underline stay empty/false (known gap)
            apiSegment.fg = apiColor( segment.foreColor );
            data.segments.push_back( std::move( apiSegment ) );
        }
        break;
    }
    }
    return data;
}

// Modification time in Unix epoch milliseconds, 0 when it cannot be read (the
// invalid QDateTime of QFileInfo::lastModified mapped to 0). file_clock has no
// to_sys() on MSVC, so both clocks are aligned at now().
int64_t lastModifiedMs( const std::filesystem::path& path )
{
    std::error_code error;
    const auto modified = std::filesystem::last_write_time( path, error );
    if ( error ) {
        return 0;
    }
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        modified - std::filesystem::file_time_type::clock::now()
        + std::chrono::system_clock::now() );
    return std::chrono::duration_cast<std::chrono::milliseconds>( systemTime.time_since_epoch() )
        .count();
}

// Size in bytes, 0 when the file cannot be read (QFileInfo::size reported 0 too)
int64_t fileSizeBytes( const std::filesystem::path& path )
{
    std::error_code error;
    const auto size = std::filesystem::file_size( path, error );
    return error ? int64_t{ 0 } : static_cast<int64_t>( size );
}

// Gutter bits of one source line, shared by the range and batch lineFlags
// variants; the event bit mirrors the file's latest completed extraction.
// The caller holds the file entry's fieldsMutex_ (shared) and the read token
// holds the filtered data's lock, so the whole batch sees one snapshot.
uint8_t lineFlagBits( const EngineContext::FileEntry& fileEntry,
                      const LogFilteredData::ReadToken* readToken, LineNumber line )
{
    uint8_t flagBits = 0;
    if ( fileEntry.marks.count( line ) ) {
        flagBits |= LineFlagMark;
    }
    if ( readToken
         && readToken->lineTypeByLine( line ).testFlag(
             LogFilteredData::LineTypeFlags::Match ) ) {
        flagBits |= LineFlagMatch;
    }
    if ( fileEntry.eventLines.count( line ) ) {
        flagBits |= LineFlagEvent;
    }
    return flagBits;
}

// The regex engines reject duplicate named groups in a combined alternation, so
// rule patterns are flattened to non-capturing groups for the search; the
// individual patterns are re-applied during extraction to recover parameters
std::string flattenNamedGroups( const std::string& pattern )
{
    static const Pcre2Regex namedGroupRx{ R"(\(\?<[^>]+>)" };

    std::string result;
    result.reserve( pattern.size() );
    size_t copiedUpTo = 0;
    for ( auto matches = namedGroupRx.globalMatch( pattern ); matches.hasNext(); ) {
        const auto match = matches.next();
        const auto start = static_cast<size_t>( match.capturedStart() );
        const auto end = static_cast<size_t>( match.capturedEnd() );
        result.append( pattern, copiedUpTo, start - copiedUpTo );
        result.append( "(?:" );
        copiedUpTo = end;
    }
    result.append( pattern, copiedUpTo, pattern.size() - copiedUpTo );
    return result;
}

// Copy of the cached file info under the entry lock
FileInfo snapshotInfo( const EngineContext::FileEntry& entry )
{
    SharedLock lock( entry.fieldsMutex_ );
    return entry.info;
}

// ====== Shared-state helpers (moved out of the deleted per-session store) ======

klogg::Rgba apiColorToRgba( const std::string& color )
{
    if ( color.empty() ) {
        return {};
    }
    if ( const auto parsed = klogg::Rgba::fromHex( color ) ) {
        return *parsed;
    }
    if ( color.size() == 7 && color[ 0 ] == '#' ) {
        if ( const auto parsed = klogg::Rgba::fromHex( "#ff" + color.substr( 1 ) ) ) {
            return *parsed;
        }
    }
    return {};
}

std::string rgbaToApiColor( const klogg::Rgba& color )
{
    return color.toHex();
}

Highlighter highlighterFromApiRule( const HighlightRule& apiRule )
{
    Highlighter rule{ apiRule.pattern, !apiRule.caseSensitive, apiRule.onlyMatch,
                      apiColorToRgba( apiRule.color ), apiColorToRgba( apiRule.backColor ) };
    rule.setName( apiRule.name );
    rule.setUseRegex( apiRule.useRegex );
    rule.setVariateColors( apiRule.variateColors );
    rule.setColorVariance( apiRule.colorVariance );
    rule.setUseThemeBack( apiRule.useThemeBack );
    rule.setEnabled( apiRule.enabled );
    if ( apiRule.useThemeBack ) {
        // A theme background means there is no explicit back color
        rule.setBackColor( klogg::Rgba{} );
    }
    return rule;
}

HighlightRule apiRuleFromHighlighter( const Highlighter& rule )
{
    HighlightRule apiRule;
    apiRule.name = rule.name();
    apiRule.pattern = rule.pattern();
    apiRule.useRegex = rule.useRegex();
    apiRule.caseSensitive = !rule.ignoreCase();
    apiRule.color = rgbaToApiColor( rule.foreColor() );
    apiRule.backColor = rgbaToApiColor( rule.backColor() );
    apiRule.useThemeBack = rule.useThemeBack();
    apiRule.onlyMatch = rule.highlightOnlyMatch();
    apiRule.variateColors = rule.variateColors();
    apiRule.colorVariance = rule.colorVariance();
    apiRule.enabled = rule.isEnabled();
    return apiRule;
}

QuickHighlighter quickHighlighterFromApiDef( const QuickHighlighterDef& def )
{
    QuickHighlighter quickHighlighter;
    quickHighlighter.name = def.name;
    quickHighlighter.color.foreColor = apiColorToRgba( def.color );
    quickHighlighter.color.backColor = apiColorToRgba( def.backColor );
    quickHighlighter.useInCycle = def.useInCycle;
    return quickHighlighter;
}

// The session JSON accessors keep the leniency of the QJsonValue ones they
// replace: an absent key or a value of another type yields the fallback, so old
// and hand-edited session files stay loadable
const nlohmann::json* jsonMember( const nlohmann::json& object, const char* key )
{
    if ( !object.is_object() ) {
        return nullptr;
    }
    const auto member = object.find( key );
    return member != object.end() ? &( *member ) : nullptr;
}

std::string jsonString( const nlohmann::json& object, const char* key )
{
    const auto* value = jsonMember( object, key );
    return value != nullptr && value->is_string() ? value->get<std::string>() : std::string{};
}

bool jsonBool( const nlohmann::json& object, const char* key, bool fallback )
{
    const auto* value = jsonMember( object, key );
    return value != nullptr && value->is_boolean() ? value->get<bool>() : fallback;
}

int jsonInt( const nlohmann::json& object, const char* key, int fallback )
{
    const auto* value = jsonMember( object, key );
    return value != nullptr && value->is_number_integer() ? value->get<int>() : fallback;
}

nlohmann::json jsonArray( const nlohmann::json& object, const char* key )
{
    const auto* value = jsonMember( object, key );
    return value != nullptr && value->is_array() ? *value : nlohmann::json::array();
}

nlohmann::json jsonObject( const nlohmann::json& value )
{
    return value.is_object() ? value : nlohmann::json::object();
}

} // namespace

std::shared_ptr<EngineContext::FileEntry> EngineContext::findFile( FileId id ) const
{
    SharedLock lock( entriesMutex_ );
    const auto it = entries.find( id );
    return it != entries.end() ? it->second : nullptr;
}

std::shared_ptr<EngineContext::FileEntry>
EngineContext::findFileByNormalizedPath( const std::string& normalizedPath ) const
{
    SharedLock lock( entriesMutex_ );
    const auto it = std::find_if( entries.begin(), entries.end(), [ & ]( const auto& pair ) {
        return pair.second->normalizedPath == normalizedPath;
    } );
    return it != entries.end() ? it->second : nullptr;
}

std::pair<std::shared_ptr<EngineContext::FileEntry>, EngineContext::SearchEntry*>
EngineContext::findSearchEntry( SearchId id ) const
{
    SharedLock lock( entriesMutex_ );
    for ( auto& [ fileId, fileEntry ] : entries ) {
        SharedLock searchesLock( fileEntry->searchesMutex_ );
        const auto it = fileEntry->searches.find( id );
        if ( it != fileEntry->searches.end() ) {
            // The node is stable: searches are never individually erased and
            // the returned file entry keeps the whole map alive
            return { fileEntry, const_cast<SearchEntry*>( &it->second ) };
        }
    }
    return { nullptr, nullptr };
}

std::pair<std::shared_ptr<EngineContext::FileEntry>, EngineContext::ExtractionEntry*>
EngineContext::findExtractionEntry( ExtractionId id ) const
{
    SharedLock lock( entriesMutex_ );
    for ( auto& [ fileId, fileEntry ] : entries ) {
        SharedLock searchesLock( fileEntry->searchesMutex_ );
        const auto it = fileEntry->extractions.find( id );
        if ( it != fileEntry->extractions.end() ) {
            return { fileEntry, const_cast<ExtractionEntry*>( &it->second ) };
        }
    }
    return { nullptr, nullptr };
}

// The cancelled check and the reference copy share one stateMutex scope so a
// concurrent cancelSearch cannot free the filtered data mid-resolution
Result<std::shared_ptr<LogFilteredData>>
EngineContext::searchFilteredData( const SearchEntry& entry, SearchId id ) const
{
    std::lock_guard lock( entry.stateMutex );
    if ( entry.cancelled ) {
        return tl::make_unexpected(
            Error{ ErrorCode::SearchCancelled, "search cancelled: " + std::to_string( id ) } );
    }
    return entry.filteredData;
}

Result<FileInfo> EngineContext::beginOpenFileOnWorker( FileId id, const std::string& path,
                                                       const std::optional<std::string>& encoding,
                                                       std::string normalizedPath,
                                                       bool allowMissing )
{
    const auto filePath = platform::paths::pathFromUtf8( path );
    std::error_code fileError;
    const auto isRegularFile = std::filesystem::is_regular_file( filePath, fileError );
    // attachFile happily reports a missing file as empty-and-successful, so
    // existence has to be checked here unless the caller opts into the
    // lenient behavior (Qt UI: follow not-yet-created logs)
    if ( !allowMissing && !isRegularFile ) {
        return tl::make_unexpected( Error{ ErrorCode::FileNotFound, path } );
    }

    if ( encoding && !klogg::enc::fromName( *encoding ) ) {
        return tl::make_unexpected(
            Error{ ErrorCode::EncodingError, "unknown encoding: " + *encoding } );
    }

    // The executor is captured directly (not via this): a LogData whose last
    // reference outlives the EngineContext still posts its teardown to a
    // valid (possibly stopped) executor instead of dereferencing dead state.
    // The LogData owns its queued posts; the executor drops tasks queued
    // after stop(), like the old queued connections after the thread quit.
    const auto postTarget = [ executor = this->executor ]( std::function<void()> task ) {
        executor->post( std::move( task ) );
    };
    auto logData = std::shared_ptr<LogData>(
        new LogData( postTarget,
                     LogDataCallbacks{
                         .onLoadingProgressed = [ this, id ]( int progress ) {
                             KLOGG_TRACE_COUNTER( "engine", "indexing progress (%)", progress );
                             emitEvent( "indexing.progress",
                                        "{\"fileId\":" + std::to_string( id )
                                            + ",\"progress\":" + std::to_string( progress ) + "}" );
                         },
                         .onLoadingFinished = [ this, id ]( LoadingStatus status ) {
                             onOpenIndexingFinished( id, status );
                         },
                         .onFileChanged = [ this, id ]( MonitoredFileStatus status ) {
                             const auto fileEntry = findFile( id );
                             if ( !fileEntry || !fileEntry->watchEnabled ) {
                                 return;
                             }
                             const auto* reason
                                 = status == MonitoredFileStatus::Truncated ? "truncated" : "modified";
                             emitEvent( "file.changed", "{\"fileId\":" + std::to_string( id )
                                                            + ",\"reason\":\"" + reason + "\"}" );
                         },
                         .onError = [ this, id ]( EngineErrorKind kind,
                                                  const std::string& message ) {
                             // Engine failures are surfaced as a session event: the engine
                             // knows what went wrong, the shell decides which UI action
                             // (message box versus issue report) it maps to
                             emitEvent( "engine.error",
                                        "{\"fileId\":" + std::to_string( id ) + ",\"kind\":\""
                                            + engineErrorKindName( kind ) + "\",\"message\":\""
                                            + jsonEscape( message ) + "\"}" );
                         },
                     }, optionsHolder ),
        [ executor = this->executor ]( LogData* p ) { deleteOnExecutor( executor, p ); } );
    try {
        logData->attachFile( path );
    } catch ( ... ) {
        return tl::make_unexpected( Error{ ErrorCode::InternalError, "attachFile failed" } );
    }

    auto entry = std::make_shared<FileEntry>();
    entry->logData = std::move( logData );
    entry->info.id = id;
    entry->info.path = path;
    entry->info.size_bytes = fileSizeBytes( filePath );
    entry->info.total_lines = 0_lcount;
    entry->info.max_line_length = 0_length;
    entry->info.encoding = { "UTF-8", 1.0f };
    entry->info.last_modified_ms = lastModifiedMs( filePath );
    entry->info.indexed = false;
    entry->normalizedPath = std::move( normalizedPath );
    entry->indexingInProgress = true;
    entry->requestedEncoding = encoding;

    const auto info = entry->info;
    {
        UniqueLock lock( entriesMutex_ );
        entries.emplace( id, std::move( entry ) );
    }
    return info;
}

void EngineContext::onOpenIndexingFinished( FileId id, LoadingStatus status )
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry ) {
        // Closed while indexing; the LogData teardown is already posted
        return;
    }

    if ( !fileEntry->indexingInProgress ) {
        // Reloads (natural growth, UI-driven reload, encoding change) also
        // deliver loadingFinished through the completion callback: keep the
        // cached FileInfo in sync so API readers (line fetching reads counts
        // from here) see the new file geometry. The completion callback runs
        // before the loadingFinished listeners, in registration order, so this
        // refresh happens before reloadFile's waiter resumes.
        if ( status == LoadingStatus::Successful ) {
            refreshFileInfo( *fileEntry );
            // Keep info.encoding tracking the codec the index actually uses:
            // the forced codec when one is set, otherwise the detection,
            // which a natural full reindex re-runs on the new content. A
            // consumer re-applying the reported encoding after a content
            // change (CrawlerWidget::updateEncoding in auto mode) then
            // no-ops when the detection is unchanged and reindexes only when
            // it genuinely changed
            auto indexCodec = fileEntry->logData->getForcedEncoding();
            if ( !indexCodec ) {
                indexCodec = fileEntry->logData->getDetectedEncoding();
            }
            {
                UniqueLock lock( fileEntry->fieldsMutex_ );
                // An entry whose initial indexing was interrupted (kept open
                // for recovery) becomes indexed once any later reindex
                // completes
                fileEntry->info.indexed = true;
                if ( indexCodec ) {
                    fileEntry->info.encoding = { indexCodec->name, 1.0f };
                }
            }
            // The engine re-adds the file to the watcher on every successful
            // indexing, so the preference has to be re-applied here (covers
            // the async reload paths; the sync variants re-apply their own)
            applyWatchPreference( *fileEntry );
            uint64_t totalLines = 0;
            int64_t sizeBytes = 0;
            int64_t lastModifiedMs = 0;
            {
                SharedLock lock( fileEntry->fieldsMutex_ );
                totalLines = fileEntry->info.total_lines.get();
                sizeBytes = fileEntry->info.size_bytes;
                lastModifiedMs = fileEntry->info.last_modified_ms;
            }
            // Emitted outside the lock: a synchronous subscriber may call back
            // into the API, and no callback may run while the mutex is held
            emitEvent( "file.reindexed", "{\"fileId\":" + std::to_string( id )
                                             + ",\"totalLines\":" + std::to_string( totalLines )
                                             + ",\"sizeBytes\":" + std::to_string( sizeBytes )
                                             + ",\"lastModifiedMs\":"
                                             + std::to_string( lastModifiedMs ) + "}" );
            const auto callbacks = std::move( fileEntry->pendingReindexCallbacks );
            fileEntry->pendingReindexCallbacks.clear();
            for ( const auto& callback : callbacks ) {
                ( *callback )( snapshotInfo( *fileEntry ) );
            }
        }
        else {
            // Non-successful reindex completion. An Interrupted status can be
            // the stale emission of the operation a trigger raced with (the
            // triggered reindex reports next) or a deliberate Stop; either
            // way the views tracking this file rely on it to leave their
            // loading state, as the native loadingFinished did
            emitEvent( "file.indexing_failed", "{\"fileId\":" + std::to_string( id )
                                                   + ",\"reason\":\"" + loadingStatusName( status )
                                                   + "\"}" );
            if ( status != LoadingStatus::Interrupted
                 && !fileEntry->pendingReindexCallbacks.empty() ) {
                // Terminal failure of a triggered reindex: callbacks stay
                // pending across the stale Interrupted described above
                const auto callbacks = std::move( fileEntry->pendingReindexCallbacks );
                fileEntry->pendingReindexCallbacks.clear();
                const auto error = tl::make_unexpected(
                    Error{ ErrorCode::InternalError,
                           std::string{ "reindexing failed: " } + loadingStatusName( status ) } );
                for ( const auto& callback : callbacks ) {
                    ( *callback )( error );
                }
            }
        }
        return;
    }

    if ( status != LoadingStatus::Successful ) {
        // Fired while the entry still exists so per-file subscribers see the
        // failure of a known id; a non-interrupted open drops the entry
        // right below, and the opener is also told through onDone
        emitEvent( "file.indexing_failed", "{\"fileId\":" + std::to_string( id )
                                               + ",\"reason\":\"" + loadingStatusName( status )
                                               + "\"}" );
        const auto error = tl::make_unexpected(
            Error{ ErrorCode::InternalError,
                   std::string{ "indexing failed: " } + loadingStatusName( status ) } );
        const auto callbacks = std::move( fileEntry->pendingOpenCallbacks );
        fileEntry->pendingOpenCallbacks.clear();
        if ( status == LoadingStatus::Interrupted ) {
            // A deliberate stop (interruptLoading) is not a fatal open error:
            // keep the entry so a later reloadFile can recover the file
            fileEntry->indexingInProgress = false;
        }
        else {
            UniqueLock lock( entriesMutex_ );
            entries.erase( id );
        }
        for ( const auto& callback : callbacks ) {
            ( *callback )( error );
        }
        return;
    }

    auto* logData = fileEntry->logData.get();

    if ( fileEntry->requestedEncoding && !fileEntry->encodingApplied ) {
        fileEntry->encodingApplied = true;
        // The encoding name was validated in beginOpenFileOnWorker, and
        // LogData::doSetDisplayEncoding resolves it with the same lookup
        const auto forcedEncoding = klogg::enc::fromName( *fileEntry->requestedEncoding );
        assert( forcedEncoding );
        // Ask the engine whether this reindexes the file: a codec that only
        // changes the byte-to-character mapping (same line feed geometry)
        // leaves the index valid and completes the open here, while a geometry
        // change hands completion over to the reindex it triggers
        const bool reindexTriggered
            = logData->setDisplayEncoding( fileEntry->requestedEncoding->c_str() );
        {
            UniqueLock lock( fileEntry->fieldsMutex_ );
            fileEntry->info.encoding = { forcedEncoding->name, 1.0f };
        }
        if ( reindexTriggered ) {
            // Report only once the reindex with the forced encoding completes
            return;
        }
    }
    else if ( !fileEntry->requestedEncoding ) {
        // Sync the display codec with the detection guess (mirrors
        // CrawlerWidget::updateEncoding); otherwise lines decode with the
        // Latin-1 default of the LogData constructor. The guess is normally
        // the codec the index used, unless a default forced codec
        // (defaultEncodingMib) indexed it: applying the guess then reindexes
        // back to auto-detection, and the open may only complete once it lands
        if ( const auto detected = logData->getDetectedEncoding() ) {
            const bool reindexTriggered = logData->setDisplayEncoding( detected->name.c_str() );
            {
                UniqueLock lock( fileEntry->fieldsMutex_ );
                fileEntry->info.encoding = { detected->name, 1.0f };
            }
            if ( reindexTriggered ) {
                return;
            }
        }
    }

    fileEntry->indexingInProgress = false;
    refreshFileInfo( *fileEntry );
    {
        UniqueLock lock( fileEntry->fieldsMutex_ );
        fileEntry->info.indexed = true;
    }

    const auto info = snapshotInfo( *fileEntry );

    emitEvent( "file.indexed", "{\"fileId\":" + std::to_string( id ) + ",\"totalLines\":"
                                   + std::to_string( info.total_lines.get() ) + ",\"encoding\":\""
                                   + jsonEscape( info.encoding.name ) + "\"}" );

    const auto callbacks = std::move( fileEntry->pendingOpenCallbacks );
    fileEntry->pendingOpenCallbacks.clear();
    for ( const auto& callback : callbacks ) {
        ( *callback )( info );
    }
}

Result<FileInfo> EngineContext::waitForOpenCompletionOnWorker( FileId id )
{
    auto fileEntry = findFile( id );
    assert( fileEntry );

    std::optional<Result<FileInfo>> result;
    auto callback = std::make_shared<FileCallback>(
        [ &result, this ]( Result<FileInfo> r ) {
            result = std::move( r );
            // The callback fires on the indexing thread; the dummy post also
            // wakes the executor's pump that is waiting for the result.
            executor->post( [] {} );
        } );
    fileEntry->pendingOpenCallbacks.push_back( callback );

    // Drop the waiter and the reference this open took on every exit path,
    // including a throw out of the pump: the callback captures the stack-local
    // result, so a stale registration would be called through a dangling
    // reference by a later completion. Only a still-pending callback is
    // removed �?a completion consumed it and the open keeps the file then.
    const ScopeExitGuard unregister( [ this, id, callback ] {
        const auto entry = findFile( id );
        if ( !entry ) {
            return;
        }
        auto& callbacks = entry->pendingOpenCallbacks;
        const auto waiter = std::find( callbacks.begin(), callbacks.end(), callback );
        if ( waiter == callbacks.end() ) {
            return;
        }
        callbacks.erase( waiter );
        if ( entry->refCount > 1 ) {
            entry->refCount--;
        }
        else {
            UniqueLock lock( entriesMutex_ );
            entries.erase( id );
        }
    } );

    // The pump also services other enqueued calls while waiting
    executor->pumpUntil( [ &result ] { return result.has_value(); },
                         std::chrono::milliseconds( kIndexingTimeoutMs ) );

    if ( result ) {
        return *result;
    }

    return tl::make_unexpected( Error{ ErrorCode::InternalError, "indexing timed out" } );
}

Result<FileInfo> EngineContext::openFileOnWorker( FileId id, const std::string& path,
                                                  const std::optional<std::string>& encoding,
                                                  bool allowMissing )
{
    // Same-path detection compares the canonical path (see normalizedPathKey)
    const auto normalizedPath = platform::paths::normalizedPathKey( path );
    if ( auto fileEntry = findFileByNormalizedPath( normalizedPath ) ) {
        // Same-path open shares the entry; an encoding override only applies
        // to the first open
        fileEntry->refCount++;
        // info.id is written once before the entry is published and never
        // changes, so it is read lock-free
        const auto entryId = fileEntry->info.id;
        if ( fileEntry->indexingInProgress ) {
            return waitForOpenCompletionOnWorker( entryId );
        }
        if ( !snapshotInfo( *fileEntry ).indexed ) {
            reindexInterruptedEntry( *fileEntry );
            const auto status = waitForReindexCompletion( fileEntry->logData.get(), *executor );
            if ( !status || *status != LoadingStatus::Successful ) {
                // Roll back the reference this open took, like the timeout
                // path of waitForOpenCompletionOnWorker; the nested event
                // loop may have served a close that dropped the entry already
                const auto current = findFile( entryId );
                if ( current ) {
                    if ( current->refCount > 1 ) {
                        current->refCount--;
                    }
                    else {
                        UniqueLock lock( entriesMutex_ );
                        entries.erase( entryId );
                    }
                }
                return tl::make_unexpected( Error{
                    ErrorCode::InternalError,
                    status ? std::string{ "reindexing failed: " } + loadingStatusName( *status )
                           : std::string{ "reindexing timed out" } } );
            }
        }
        return snapshotInfo( *fileEntry );
    }

    auto pending = beginOpenFileOnWorker( id, path, encoding, normalizedPath, allowMissing );
    if ( !pending ) {
        return tl::make_unexpected( pending.error() );
    }
    return waitForOpenCompletionOnWorker( id );
}

Result<FileInfo> EngineContext::openFileAsyncOnWorker( FileId id, const std::string& path,
                                                       const std::optional<std::string>& encoding,
                                                       FileCallback onDone, bool allowMissing )
{
    // Same-path detection compares the canonical path (see normalizedPathKey)
    const auto normalizedPath = platform::paths::normalizedPathKey( path );
    if ( auto fileEntry = findFileByNormalizedPath( normalizedPath ) ) {
        fileEntry->refCount++;
        if ( fileEntry->indexingInProgress ) {
            // The callback fires when the in-flight indexing completes
            if ( onDone ) {
                fileEntry->pendingOpenCallbacks.push_back(
                    std::make_shared<FileCallback>( std::move( onDone ) ) );
            }
        }
        else if ( !snapshotInfo( *fileEntry ).indexed ) {
            // The completion of the re-triggered indexing is reported through
            // the reindex channel, like reloadFileAsync
            if ( onDone ) {
                fileEntry->pendingReindexCallbacks.push_back(
                    std::make_shared<FileCallback>( std::move( onDone ) ) );
            }
            reindexInterruptedEntry( *fileEntry );
        }
        else if ( onDone ) {
            onDone( snapshotInfo( *fileEntry ) );
        }
        return snapshotInfo( *fileEntry );
    }

    auto pending = beginOpenFileOnWorker( id, path, encoding, normalizedPath, allowMissing );
    if ( !pending ) {
        return tl::make_unexpected( pending.error() );
    }
    if ( onDone ) {
        const auto fileEntry = findFile( id );
        assert( fileEntry );
        fileEntry->pendingOpenCallbacks.push_back(
            std::make_shared<FileCallback>( std::move( onDone ) ) );
    }
    return *pending;
}

Result<void> EngineContext::closeFileOnWorker( FileId id )
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InvalidFileId, "unknown file id: " + std::to_string( id ) } );
    }

    if ( fileEntry->refCount > 1 ) {
        // Other consumers still have the file open
        fileEntry->refCount--;
        return {};
    }

    if ( fileEntry->indexingInProgress ) {
        // No completion handler runs once the entry is gone, so pending
        // openers are notified here
        const auto callbacks = std::move( fileEntry->pendingOpenCallbacks );
        const auto reindexCallbacks = std::move( fileEntry->pendingReindexCallbacks );
        {
            UniqueLock lock( entriesMutex_ );
            entries.erase( id );
        }
        const auto error
            = tl::make_unexpected( Error{ ErrorCode::FileNotOpen, "file closed while indexing" } );
        for ( const auto& callback : callbacks ) {
            ( *callback )( error );
        }
        for ( const auto& callback : reindexCallbacks ) {
            ( *callback )( error );
        }
        return {};
    }

    {
        SharedLock lock( fileEntry->searchesMutex_ );
        for ( auto& [ searchId, search ] : fileEntry->searches ) {
            if ( search.filteredData ) {
                search.filteredData->interruptSearch();
            }
        }
        for ( auto& [ extractionId, extraction ] : fileEntry->extractions ) {
            if ( extraction.filteredData ) {
                extraction.filteredData->interruptSearch();
            }
        }
    }
    const auto reindexCallbacks = std::move( fileEntry->pendingReindexCallbacks );
    {
        UniqueLock lock( entriesMutex_ );
        entries.erase( id );
    }
    // The FileEntry (and any engine objects it still owns) survives in readers
    // that hold a reference; the actual destruction is posted back to this
    // executor, searches before the LogData
    const auto error
        = tl::make_unexpected( Error{ ErrorCode::FileNotOpen, "file closed while reindexing" } );
    for ( const auto& callback : reindexCallbacks ) {
        ( *callback )( error );
    }
    return {};
}

void EngineContext::reindexInterruptedEntry( FileEntry& fileEntry )
{
    {
        // Same invalidation as reloadFile: a reindex shifts line numbers
        UniqueLock lock( fileEntry.fieldsMutex_ );
        fileEntry.eventLines.clear();
        fileEntry.eventLinesSource.reset();
    }
    syncEventLinesToSearches( fileEntry );

    fileEntry.logData->reload( fileEntry.requestedEncoding
                                   ? klogg::enc::fromName( *fileEntry.requestedEncoding )
                                   : std::nullopt );
}

Result<FileInfo> EngineContext::reloadFileOnWorker( FileId id )
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry || !fileEntry->logData ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }

    {
        // A reindex shifts line numbers, so extraction results and the event
        // gutter bits derived from them are dropped
        UniqueLock lock( fileEntry->fieldsMutex_ );
        fileEntry->eventLines.clear();
        fileEntry->eventLinesSource.reset();
    }
    syncEventLinesToSearches( *fileEntry );

    auto* logData = fileEntry->logData.get();
    // Pass the current display codec so a user-forced encoding survives the
    // reload instead of falling back to auto-detection
    logData->reload( logData->getDisplayEncoding() );

    const auto status = waitForReindexCompletion( logData, *executor );
    if ( !status ) {
        return tl::make_unexpected( Error{ ErrorCode::InternalError, "reindexing timed out" } );
    }
    if ( *status != LoadingStatus::Successful ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InternalError,
                   std::string{ "reindexing failed: " } + loadingStatusName( *status ) } );
    }

    // The loadingFinished handler (registered at open, so invoked first) has
    // already refreshed entry.info and emitted file.reindexed
    applyWatchPreference( *fileEntry );

    return snapshotInfo( *fileEntry );
}

Result<void> EngineContext::reloadFileAsyncOnWorker( FileId id, FileCallback onDone )
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry || !fileEntry->logData ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }

    {
        // Same invalidation as the synchronous reloadFile
        UniqueLock lock( fileEntry->fieldsMutex_ );
        fileEntry->eventLines.clear();
        fileEntry->eventLinesSource.reset();
    }
    syncEventLinesToSearches( *fileEntry );

    if ( onDone ) {
        fileEntry->pendingReindexCallbacks.push_back(
            std::make_shared<FileCallback>( std::move( onDone ) ) );
    }

    auto* logData = fileEntry->logData.get();
    // Same codec preservation as the synchronous reloadFile; the completion
    // is reported by onOpenIndexingFinished draining pendingReindexCallbacks
    logData->reload( logData->getDisplayEncoding() );
    return {};
}

Result<void> EngineContext::interruptLoadingOnWorker( FileId id )
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry || !fileEntry->logData ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }

    fileEntry->logData->interruptLoading();
    return {};
}

void EngineContext::refreshFileInfo( FileEntry& entry )
{
    const auto* logData = entry.logData.get();
    // Engine reads first (they take the index/file locks), then the cached
    // fields are committed under the entry lock
    const auto sizeBytes
        = fileSizeBytes( platform::paths::pathFromUtf8( snapshotInfo( entry ).path ) );
    const auto totalLines = logData->getNbLine();
    const auto maxLineLength = logData->getMaxLength();
    const auto lastModified = logData->getLastModifiedDate();
    const auto lastModifiedMs
        = lastModified ? std::chrono::duration_cast<std::chrono::milliseconds>(
                             lastModified->time_since_epoch() )
                             .count()
                       : int64_t{ 0 };

    UniqueLock lock( entry.fieldsMutex_ );
    entry.info.size_bytes = sizeBytes;
    entry.info.total_lines = totalLines;
    entry.info.max_line_length = maxLineLength;
    entry.info.last_modified_ms = lastModifiedMs;
}

void EngineContext::applyWatchPreference( const FileEntry& entry )
{
    auto& watcher = FileWatcher::getFileWatcher();
    if ( entry.watchEnabled ) {
        watcher.addFile( snapshotInfo( entry ).path );
    }
    else {
        watcher.removeFile( snapshotInfo( entry ).path );
    }
}

Result<void> EngineContext::watchFileOnWorker( FileId id, bool enabled )
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry || !fileEntry->logData ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }

    fileEntry->watchEnabled = enabled;
    applyWatchPreference( *fileEntry );
    return {};
}

Result<void> EngineContext::checkFileChangesOnWorker( FileId id )
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry || !fileEntry->logData ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }

    // Same entry point the native watcher uses: it stats the file and queues a
    // CheckDataChangesOperation, which turns appends into PartialReindex only.
    fileEntry->logData->checkFileChanges();
    return {};
}

Result<void> EngineContext::setEncodingOnWorker( FileId id, const std::string& encoding )
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry || !fileEntry->logData ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }

    const auto requestedEncoding = klogg::enc::fromName( encoding );
    if ( !requestedEncoding ) {
        return tl::make_unexpected(
            Error{ ErrorCode::EncodingError, "unknown encoding: " + encoding } );
    }

    auto* logData = fileEntry->logData.get();
    // The engine reports whether the new codec reindexes the file: only then
    // is there a completion to wait for (a codec that changes bytes to
    // characters but not the line feed geometry applies right away)
    const bool reindexTriggered = logData->setDisplayEncoding( encoding.c_str() );
    if ( reindexTriggered ) {
        const auto status = waitForReindexCompletion( logData, *executor );
        if ( !status || *status != LoadingStatus::Successful ) {
            return tl::make_unexpected(
                Error{ ErrorCode::InternalError, "reindexing with the new encoding failed" } );
        }
    }

    {
        UniqueLock lock( fileEntry->fieldsMutex_ );
        fileEntry->info.encoding = { requestedEncoding->name, 1.0f };
    }
    refreshFileInfo( *fileEntry );
    applyWatchPreference( *fileEntry );
    return {};
}

Result<void> EngineContext::setEncodingAsyncOnWorker( FileId id, const std::string& encoding,
                                                      FileCallback onDone )
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry || !fileEntry->logData ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }

    const auto requestedEncoding = klogg::enc::fromName( encoding );
    if ( !requestedEncoding ) {
        return tl::make_unexpected(
            Error{ ErrorCode::EncodingError, "unknown encoding: " + encoding } );
    }

    auto* logData = fileEntry->logData.get();

    // Applied (and any reindex triggered) before returning, so a repaint
    // right after this call already uses the new codec
    const bool reindexTriggered = logData->setDisplayEncoding( encoding.c_str() );
    {
        UniqueLock lock( fileEntry->fieldsMutex_ );
        fileEntry->info.encoding = { requestedEncoding->name, 1.0f };
    }

    if ( !reindexTriggered ) {
        refreshFileInfo( *fileEntry );
        applyWatchPreference( *fileEntry );
        if ( onDone ) {
            onDone( snapshotInfo( *fileEntry ) );
        }
        return {};
    }

    // The triggered reindex completes asynchronously; onOpenIndexingFinished
    // refreshes the file geometry and drains pendingReindexCallbacks
    if ( onDone ) {
        fileEntry->pendingReindexCallbacks.push_back(
            std::make_shared<FileCallback>( std::move( onDone ) ) );
    }
    return {};
}

Result<FileInfo> EngineContext::fileInfo( FileId id ) const
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }
    return snapshotInfo( *fileEntry );
}

Result<std::vector<FileInfo>> EngineContext::openFiles() const
{
    std::vector<FileInfo> files;
    SharedLock lock( entriesMutex_ );
    files.reserve( entries.size() );
    for ( const auto& [ id, entry ] : entries ) {
        files.push_back( snapshotInfo( *entry ) );
    }
    return files;
}

Result<std::vector<LineData>> EngineContext::getLines( FileId id, LineNumber from,
                                                       LinesCount count, AnsiMode ansi ) const
{
    KLOGG_TRACE_SCOPE( "engine", "EngineContext::getLines" );
    const auto fileEntry = findFile( id );
    if ( !fileEntry || !fileEntry->logData ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }

    auto* logData = fileEntry->logData.get();
    std::vector<LineData> result;
    const auto nbLines = logData->getNbLine().get();
    if ( from.get() >= nbLines || count.get() == 0 ) {
        return result;
    }

    // LogData::getLines pads out-of-range requests with warning lines instead
    // of truncating, so the range is clamped here
    const auto clampedCount = std::min( count.get(), nbLines - from.get() );
    auto lines = logData->getLines( from, LinesCount( clampedCount ) );
    result.reserve( lines.size() );

    auto lineNum = from;
    for ( auto& line : lines ) {
        result.push_back( convertLine( std::move( line ), lineNum, ansi ) );
        lineNum += 1_lcount;
    }
    return result;
}

Result<std::vector<LineData>>
EngineContext::getLinesBatch( FileId id, const std::vector<LineNumber>& lines, AnsiMode ansi ) const
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry || !fileEntry->logData ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }

    auto rawLines = fileEntry->logData->getLinesForLineNumbers(
        klogg::vector<LineNumber>( lines.begin(), lines.end() ) );
    std::vector<LineData> result;
    // The batch contract is one entry per requested line; clamp before
    // indexing the request array (defense in depth, the engine pads too)
    const auto batchSize = std::min( rawLines.size(), lines.size() );
    result.reserve( batchSize );
    for ( size_t i = 0; i < batchSize; ++i ) {
        result.push_back( convertLine( std::move( rawLines[ i ] ), lines[ i ], ansi ) );
    }
    return result;
}

Result<SearchId> EngineContext::runSearchOnWorker( FileId id, SearchId searchId,
                                                   const SearchParams& params )
{
    KLOGG_TRACE_SCOPE( "engine", "EngineContext::runSearch" );
    const auto fileEntry = findFile( id );
    if ( !fileEntry || !fileEntry->logData ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }

    auto* logData = fileEntry->logData.get();
    // The custom deleter posts the destruction back to the executor and holds
    // the LogData alive until the deletion ran
    auto filteredData = wrapFilteredData( logData->getNewFilteredData(), executor,
                                          fileEntry->logData );
    // The default visibility is Marks|Matches; the API keeps marks at session
    // level, so restrict the filtered view to plain matches
    filteredData->setVisibility( LogFilteredData::VisibilityFlags::Matches );
    const RegularExpressionPattern pattern( params.pattern, params.caseSensitive,
                                            params.invertMatch, params.isBoolean,
                                            !params.isRegex /* plainText */ );

    SearchEntry* entry = nullptr;
    {
        UniqueLock lock( fileEntry->searchesMutex_ );
        entry = &fileEntry->searches.try_emplace( searchId ).first->second;
        entry->filteredData = std::move( filteredData );
        entry->maxResults = params.maxResults;
        entry->params = params;
        entry->startTime = std::chrono::steady_clock::now();
        entry->startTotalLines = logData->getNbLine();
    }
    // The node pointer stays valid: searches are never individually erased
    // and the file entry is held for the whole call

    // A new search picks up the file's already-extracted event lines so an
    // Events visibility has them from the start
    syncEventLinesToSearches( *fileEntry );

    entry->filteredData->addMatchesSearchProgressedListener(
        [ this, id, searchId ]( LinesCount nbMatches, int progress, LineNumber ) {
            onSearchProgressed( id, searchId, nbMatches, progress );
        } );

    const auto startLine = params.fromLine.value_or( 0_lnum );
    // Engine endLine is exclusive (one past the last line to scan); toLine
    // passes through with the same semantics
    const auto endLine = params.toLine.value_or( LineNumber( logData->getNbLine().get() ) );
    entry->scannedToLine = endLine;
    entry->filteredData->runSearch( pattern, startLine, endLine );
    return searchId;
}

Result<void> EngineContext::extendSearchOnWorker( SearchId id, LineNumber toLine )
{
    const auto [ fileEntry, entry ] = findSearchEntry( id );
    if ( !fileEntry || !entry || !entry->filteredData ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InvalidSearchId, "unknown search id: " + std::to_string( id ) } );
    }

    std::shared_ptr<LogFilteredData> filteredData;
    SearchParams params;
    LineNumber previousEnd;
    {
        std::lock_guard lock( entry->stateMutex );
        if ( !entry->completed ) {
            return tl::make_unexpected(
                Error{ ErrorCode::InvalidSearchId, "search is still running" } );
        }
        if ( entry->maxResults && entry->totalMatches.get() >= *entry->maxResults ) {
            return {};
        }
        filteredData = entry->filteredData;
        params = entry->params;
        previousEnd = entry->scannedToLine;
        if ( toLine <= previousEnd ) {
            return {};
        }
        entry->completed = false;
        entry->progressPercent = 0;
        entry->startTime = std::chrono::steady_clock::now();
    }

    const auto startLine = params.fromLine.value_or( 0_lnum );
    const RegularExpressionPattern pattern( params.pattern, params.caseSensitive,
                                            params.invertMatch, params.isBoolean,
                                            !params.isRegex /* plainText */ );

    // The filtered data validates the existing key; a mismatch or an
    // unfinished worker falls back to a full search so the result stays exact.
    if ( !filteredData->extendSearch( pattern, toLine ) ) {
        filteredData->runSearch( pattern, startLine, toLine );
    }

    {
        std::lock_guard lock( entry->stateMutex );
        entry->params.toLine = toLine;
        entry->scannedToLine = toLine;
    }
    return {};
}

void EngineContext::onSearchProgressed( FileId fileId, SearchId searchId, LinesCount nbMatches,
                                        int progress )
{
    const auto [ fileEntry, searchEntry ] = findSearchEntry( searchId );
    if ( !fileEntry || fileEntry->info.id != fileId ) {
        return;
    }
    auto& entry = *searchEntry;

    {
        std::lock_guard lock( entry.stateMutex );
        entry.totalMatches = nbMatches;
        entry.progressPercent = progress;
    }

    const auto ratio = std::clamp( static_cast<float>( progress ) / 100.0f, 0.0f, 1.0f );
    // Snapshot before invoking: a callback may register another one
    // (onSearchProgress appends to the same vector), which would invalidate
    // the iterators of an in-place traversal
    const auto callbacks = entry.callbacks;
    for ( const auto& cb : callbacks ) {
        cb( searchId, nbMatches, ratio );
    }
    emitEvent( "search.progress", "{\"searchId\":" + std::to_string( searchId )
                                      + ",\"matched\":" + std::to_string( nbMatches.get() )
                                      + ",\"ratio\":" + std::to_string( ratio ) + "}" );

    const auto maxResultsReached = entry.maxResults && nbMatches.get() >= *entry.maxResults;
    auto justCompleted = false;
    {
        std::lock_guard lock( entry.stateMutex );
        if ( !entry.completed && ( progress >= 100 || maxResultsReached ) ) {
            entry.completed = true;
            justCompleted = true;
        }
    }

    if ( justCompleted ) {
        std::shared_ptr<LogFilteredData> filteredData;
        {
            std::lock_guard lock( entry.stateMutex );
            filteredData = entry.filteredData;
        }
        if ( progress < 100 && filteredData ) {
            // Stopping at maxResults still counts as a completed search
            filteredData->interruptSearch();
        }
        const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - entry.startTime )
                                    .count();
        emitEvent( "search.completed",
                   "{\"searchId\":" + std::to_string( searchId )
                       + ",\"totalMatches\":" + std::to_string( nbMatches.get() )
                       + ",\"durationMs\":" + std::to_string( durationMs ) + "}" );
    }
}

Result<SearchResult> EngineContext::searchProgress( SearchId id ) const
{
    const auto [ fileEntry, entry ] = findSearchEntry( id );
    if ( !entry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InvalidSearchId, "unknown search id: " + std::to_string( id ) } );
    }

    std::lock_guard lock( entry->stateMutex );
    if ( entry->cancelled ) {
        return tl::make_unexpected(
            Error{ ErrorCode::SearchCancelled, "search cancelled: " + std::to_string( id ) } );
    }

    SearchResult result;
    result.id = id;
    result.total_matches = entry->totalMatches;
    // The engine exposes no processed-lines counter; approximate it from the
    // progress percentage over the line count at search start
    result.processed_lines = LinesCount( entry->startTotalLines.get()
                                         * static_cast<uint64_t>( entry->progressPercent ) / 100 );
    result.progress
        = std::clamp( static_cast<float>( entry->progressPercent ) / 100.0f, 0.0f, 1.0f );
    return result;
}

Result<std::vector<LineNumber>> EngineContext::searchMatches( SearchId id, uint64_t from,
                                                              uint32_t count ) const
{
    const auto [ fileEntry, entry ] = findSearchEntry( id );
    if ( !entry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InvalidSearchId, "unknown search id: " + std::to_string( id ) } );
    }
    const auto filteredData = searchFilteredData( *entry, id );
    if ( !filteredData ) {
        return tl::make_unexpected( filteredData.error() );
    }

    std::vector<LineNumber> matches;
    // One array snapshot for the whole batch: the union projection is bounded
    // by the pure-match count (B5a coupling note)
    const auto token = ( *filteredData )->readToken();
    const auto nbMatches = token.getNbMatches().get();
    if ( from >= nbMatches ) {
        return matches;
    }

    const auto end = std::min( from + count, nbMatches );
    matches.reserve( end - from );
    for ( auto index = from; index < end; ++index ) {
        matches.push_back( token.getMatchingLineNumber(
            LineNumber( static_cast<LineNumber::UnderlyingType>( index ) ) ) );
    }
    return matches;
}

Result<std::optional<uint64_t>> EngineContext::searchMatchIndex( SearchId id,
                                                                 LineNumber line ) const
{
    const auto [ fileEntry, entry ] = findSearchEntry( id );
    if ( !entry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InvalidSearchId, "unknown search id: " + std::to_string( id ) } );
    }
    const auto filteredData = searchFilteredData( *entry, id );
    if ( !filteredData ) {
        return tl::make_unexpected( filteredData.error() );
    }

    const auto token = ( *filteredData )->readToken();
    const auto nbMatches = token.getNbMatches().get();
    if ( nbMatches == 0 ) {
        return std::optional<uint64_t>{};
    }

    // getLineIndexNumber is rank(line)-1 clamped at 0 (rank counts matches
    // <= line), i.e. the index of the greatest match at or before `line`
    const auto indexBefore = token.getLineIndexNumber( line ).get();
    const auto lineAtIndex = token.getMatchingLineNumber(
        LineNumber( static_cast<LineNumber::UnderlyingType>( indexBefore ) ) );

    uint64_t index;
    if ( lineAtIndex == line ) {
        index = indexBefore;
    }
    else if ( lineAtIndex < line ) {
        // The first match past `line` is the next one
        index = indexBefore + 1;
    }
    else {
        // Only possible with indexBefore == 0: every match is past `line`
        index = 0;
    }

    if ( index >= nbMatches ) {
        return std::optional<uint64_t>{};
    }
    return index;
}

Result<std::vector<LineData>> EngineContext::getFilteredLines( SearchId id, uint64_t fromIndex,
                                                               uint32_t count, AnsiMode ansi ) const
{
    const auto [ fileEntry, entry ] = findSearchEntry( id );
    if ( !entry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InvalidSearchId, "unknown search id: " + std::to_string( id ) } );
    }
    const auto filteredData = searchFilteredData( *entry, id );
    if ( !filteredData ) {
        return tl::make_unexpected( filteredData.error() );
    }

    // Resolve the projection under one snapshot so the fetched text and the
    // reported source line numbers cannot diverge mid-batch
    std::vector<LineNumber> sourceLines;
    {
        const auto token = ( *filteredData )->readToken();
        const auto nbLines = token.getNbLine().get();
        if ( fromIndex >= nbLines || count == 0 ) {
            return std::vector<LineData>{};
        }

        const auto clampedCount = std::min<uint64_t>( count, nbLines - fromIndex );
        sourceLines.reserve( clampedCount );
        for ( uint64_t i = 0; i < clampedCount; ++i ) {
            sourceLines.push_back( token.getMatchingLineNumber(
                LineNumber( static_cast<LineNumber::UnderlyingType>( fromIndex + i ) ) ) );
        }
    }

    // The search's filtered data reads its text from the owning file
    auto lines = fileEntry->logData->getLinesForLineNumbers(
        klogg::vector<LineNumber>( sourceLines.begin(), sourceLines.end() ) );

    std::vector<LineData> result;
    // Same defense as getLinesBatch: never index past the shorter array
    const auto batchSize = std::min( lines.size(), sourceLines.size() );
    result.reserve( batchSize );
    for ( size_t i = 0; i < batchSize; ++i ) {
        result.push_back( convertLine( std::move( lines[ i ] ), sourceLines[ i ], ansi ) );
    }
    return result;
}

Result<std::vector<LineNumber>>
EngineContext::filteredLineNumbers( SearchId id, uint64_t fromIndex, uint32_t count ) const
{
    const auto [ fileEntry, entry ] = findSearchEntry( id );
    if ( !entry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InvalidSearchId, "unknown search id: " + std::to_string( id ) } );
    }
    const auto filteredData = searchFilteredData( *entry, id );
    if ( !filteredData ) {
        return tl::make_unexpected( filteredData.error() );
    }

    std::vector<LineNumber> result;
    // Pure projection enumeration: one roaring select per line, no file read
    const auto token = ( *filteredData )->readToken();
    const auto nbLines = token.getNbLine().get();
    if ( fromIndex >= nbLines || count == 0 ) {
        return result;
    }

    const auto clampedCount = std::min<uint64_t>( count, nbLines - fromIndex );
    result.reserve( clampedCount );
    for ( uint64_t i = 0; i < clampedCount; ++i ) {
        result.push_back( token.getMatchingLineNumber(
            LineNumber( static_cast<LineNumber::UnderlyingType>( fromIndex + i ) ) ) );
    }
    return result;
}

Result<LinesCount> EngineContext::setSearchVisibilityOnWorker( SearchId id, bool matches,
                                                               bool marks, bool events )
{
    const auto [ fileEntry, searchEntry ] = findSearchEntry( id );
    if ( !searchEntry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InvalidSearchId, "unknown search id: " + std::to_string( id ) } );
    }

    auto& entry = *searchEntry;
    const auto filteredData = searchFilteredData( entry, id );
    if ( !filteredData ) {
        return tl::make_unexpected( filteredData.error() );
    }

    using VisibilityFlags = LogFilteredData::VisibilityFlags;
    LogFilteredData::Visibility visibility;
    if ( matches ) {
        visibility |= VisibilityFlags::Matches;
    }
    if ( marks ) {
        visibility |= VisibilityFlags::Marks;
    }
    if ( events ) {
        visibility |= VisibilityFlags::Events;
    }

    if ( marks ) {
        // Session-level marks are the source of truth; the engine object
        // carries them only as a render cache, so re-sync it on every
        // marks-enabling switch (same clear + re-add as the Qt UI mirror)
        std::vector<LineNumber> sessionMarks;
        {
            SharedLock lock( fileEntry->fieldsMutex_ );
            sessionMarks.assign( fileEntry->marks.begin(), fileEntry->marks.end() );
        }
        ( *filteredData )->clearMarks();
        ( *filteredData )->addMarks( sessionMarks );
    }
    // Events likewise live at file level; sync before the visibility flip
    // so an Events-enabling switch sees the populated array
    syncEventLinesToSearches( *fileEntry );
    ( *filteredData )->setVisibility( visibility );
    return ( *filteredData )->getNbLine();
}

void EngineContext::syncEventLinesToSearches( FileEntry& fileEntry )
{
    // The projections mirror the file's latest completed extraction, narrowed
    // to the selected categories; the source extraction's events carry the
    // category of each line. The event lines source and the category filter
    // are only touched on the executor, so no fields lock is taken here.
    std::set<LineNumber> selectedLines;
    if ( fileEntry.eventLinesSource ) {
        const auto extractionIt = fileEntry.extractions.find( *fileEntry.eventLinesSource );
        if ( extractionIt != fileEntry.extractions.end() ) {
            for ( const auto& event : extractionIt->second.events ) {
                if ( fileEntry.eventCategoryFilter.empty()
                     || fileEntry.eventCategoryFilter.count( event.category ) ) {
                    selectedLines.insert( event.line );
                }
            }
        }
    }

    // Cancelled searches dropped their filtered data already
    std::vector<LineNumber> selectedLineList( selectedLines.begin(), selectedLines.end() );
    SharedLock lock( fileEntry.searchesMutex_ );
    for ( auto& [ searchId, search ] : fileEntry.searches ) {
        auto* filteredData = search.filteredData.get();
        if ( !filteredData ) {
            continue;
        }
        filteredData->clearEvents();
        // One bulk merge per search instead of one per event: the event set
        // is rebuilt on several paths and the per-event path rebuilt the
        // three union bitmaps for every single line
        filteredData->addEvents( selectedLineList );
    }
}

void EngineContext::syncMarkLinesToSearches( FileEntry& fileEntry,
                                             const std::vector<LineNumber>& lines, bool added )
{
    // Runs on the executor, like every other mutation of the projections. The
    // visibility read takes the filtered data's own lock, so the searches lock
    // is all this walk holds; callers must never enter with fieldsMutex_ held.
    using VisibilityFlags = LogFilteredData::VisibilityFlags;
    SharedLock lock( fileEntry.searchesMutex_ );
    for ( auto& [ searchId, search ] : fileEntry.searches ) {
        auto* filteredData = search.filteredData.get();
        if ( !filteredData || !filteredData->visibility().testFlag( VisibilityFlags::Marks ) ) {
            continue;
        }

        if ( added ) {
            filteredData->addMarks( lines );
        }
        else {
            filteredData->removeMarks( lines );
        }
    }
}

Result<LinesCount> EngineContext::getFilteredLineCount( SearchId id ) const
{
    const auto [ fileEntry, entry ] = findSearchEntry( id );
    if ( !entry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InvalidSearchId, "unknown search id: " + std::to_string( id ) } );
    }
    const auto filteredData = searchFilteredData( *entry, id );
    if ( !filteredData ) {
        return tl::make_unexpected( filteredData.error() );
    }
    return ( *filteredData )->getNbLine();
}

Result<std::optional<uint64_t>> EngineContext::filteredLineIndex( SearchId id,
                                                                  LineNumber line ) const
{
    const auto [ fileEntry, entry ] = findSearchEntry( id );
    if ( !entry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InvalidSearchId, "unknown search id: " + std::to_string( id ) } );
    }
    const auto filteredData = searchFilteredData( *entry, id );
    if ( !filteredData ) {
        return tl::make_unexpected( filteredData.error() );
    }

    const auto token = ( *filteredData )->readToken();
    const auto nbLines = token.getNbLine().get();
    if ( nbLines == 0 ) {
        return std::optional<uint64_t>{};
    }

    // getLineIndexNumber ranks over the current visibility union
    // (rank(line)-1 clamped at 0): a projected line sits exactly at that
    // index, anything else maps to its predecessor
    const auto index = token.getLineIndexNumber( line ).get();
    const auto lineAtIndex = token.getMatchingLineNumber(
        LineNumber( static_cast<LineNumber::UnderlyingType>( index ) ) );
    if ( lineAtIndex != line ) {
        return std::optional<uint64_t>{};
    }
    return index;
}

Result<std::optional<uint64_t>>
EngineContext::filteredLineIndexAtOrBefore( SearchId id, LineNumber line ) const
{
    const auto [ fileEntry, entry ] = findSearchEntry( id );
    if ( !entry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InvalidSearchId, "unknown search id: " + std::to_string( id ) } );
    }
    const auto filteredData = searchFilteredData( *entry, id );
    if ( !filteredData ) {
        return tl::make_unexpected( filteredData.error() );
    }

    const auto token = ( *filteredData )->readToken();
    if ( token.getNbLine().get() == 0 ) {
        return std::optional<uint64_t>{};
    }

    // getLineIndexNumber is rank(line)-1 clamped at 0; rank counts the
    // members <= line, so a non-zero rank lands on the greatest member at or
    // before the line and a zero rank means the line precedes every member
    const auto index = token.getLineIndexNumber( line ).get();
    const auto lineAtIndex = token.getMatchingLineNumber(
        LineNumber( static_cast<LineNumber::UnderlyingType>( index ) ) );
    if ( lineAtIndex > line ) {
        return std::optional<uint64_t>{};
    }
    return index;
}

Result<LineLength> EngineContext::filteredMaxLineLength( SearchId id ) const
{
    const auto [ fileEntry, entry ] = findSearchEntry( id );
    if ( !entry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InvalidSearchId, "unknown search id: " + std::to_string( id ) } );
    }
    const auto filteredData = searchFilteredData( *entry, id );
    if ( !filteredData ) {
        return tl::make_unexpected( filteredData.error() );
    }
    return ( *filteredData )->getMaxLength();
}

Result<std::pair<std::shared_ptr<EngineContext::FileEntry>, std::shared_ptr<LogFilteredData>>>
EngineContext::lineFlagsSource( FileId id, std::optional<SearchId> searchId ) const
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry || !fileEntry->logData ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }

    std::shared_ptr<LogFilteredData> filteredData;
    if ( searchId ) {
        // Scoped to the searches of this file: the matches of another file's
        // search are meaningless against its line numbers
        {
            SharedLock lock( fileEntry->searchesMutex_ );
            const auto searchIt = fileEntry->searches.find( *searchId );
            if ( searchIt == fileEntry->searches.end() ) {
                return tl::make_unexpected(
                    Error{ ErrorCode::InvalidSearchId,
                           "unknown search id: " + std::to_string( *searchId ) } );
            }
            const auto filtered = searchFilteredData( searchIt->second, *searchId );
            if ( !filtered ) {
                return tl::make_unexpected( filtered.error() );
            }
            filteredData = *filtered;
        }
    }

    return std::make_pair( std::move( fileEntry ), std::move( filteredData ) );
}

Result<std::vector<uint8_t>> EngineContext::lineFlags( FileId id,
                                                       std::optional<SearchId> searchId,
                                                       LineNumber from, LinesCount count ) const
{
    const auto source = lineFlagsSource( id, searchId );
    if ( !source ) {
        return tl::make_unexpected( source.error() );
    }

    const auto& fileEntry = *source->first;
    const auto& filteredData = source->second;

    std::vector<uint8_t> flags;
    const auto nbLines = fileEntry.logData->getNbLine().get();
    if ( from.get() >= nbLines || count.get() == 0 ) {
        return flags;
    }

    // One lock for the session marks/event lines and one array snapshot for
    // the search, so the whole range sees a consistent gutter state
    SharedLock fieldsLock( fileEntry.fieldsMutex_ );
    const auto token
        = filteredData ? std::optional<LogFilteredData::ReadToken>( filteredData->readToken() )
                       : std::nullopt;

    const auto clampedCount = std::min( count.get(), nbLines - from.get() );
    flags.reserve( clampedCount );
    for ( uint64_t i = 0; i < clampedCount; ++i ) {
        flags.push_back(
            lineFlagBits( fileEntry, token ? &*token : nullptr, LineNumber( from.get() + i ) ) );
    }
    return flags;
}

Result<std::vector<uint8_t>>
EngineContext::lineFlagsBatch( FileId id, std::optional<SearchId> searchId,
                               const std::vector<LineNumber>& lines ) const
{
    const auto source = lineFlagsSource( id, searchId );
    if ( !source ) {
        return tl::make_unexpected( source.error() );
    }

    const auto& fileEntry = *source->first;
    const auto& filteredData = source->second;
    const auto nbLines = fileEntry.logData->getNbLine().get();

    SharedLock fieldsLock( fileEntry.fieldsMutex_ );
    const auto token
        = filteredData ? std::optional<LogFilteredData::ReadToken>( filteredData->readToken() )
                       : std::nullopt;

    // Out-of-file lines come back plain instead of failing the batch: a frame
    // queried across a truncation must not lose its whole gutter
    std::vector<uint8_t> flags;
    flags.reserve( lines.size() );
    for ( const auto line : lines ) {
        flags.push_back( line.get() < nbLines
                             ? lineFlagBits( fileEntry, token ? &*token : nullptr, line )
                             : 0 );
    }
    return flags;
}

Result<double> EngineContext::scrollMultiplier( FileId id ) const
{
    const auto info = fileInfo( id );
    if ( !info ) {
        return tl::make_unexpected( info.error() );
    }
    return scrollMultiplierForLines( info->total_lines );
}

Result<LineNumber> EngineContext::scrollPosToLine( FileId id, int64_t scrollPos ) const
{
    const auto multiplier = scrollMultiplier( id );
    if ( !multiplier ) {
        return tl::make_unexpected( multiplier.error() );
    }

    // A scroll bar never reports a negative position; reading one as the first
    // line keeps the line number of the mapping in range
    const auto position = std::max<int64_t>( scrollPos, 0 );
    return LineNumber( static_cast<LineNumber::UnderlyingType>(
        std::llround( static_cast<double>( position ) / *multiplier ) ) );
}

Result<int64_t> EngineContext::lineToScrollPos( FileId id, LineNumber line ) const
{
    const auto multiplier = scrollMultiplier( id );
    if ( !multiplier ) {
        return tl::make_unexpected( multiplier.error() );
    }
    return std::llround( static_cast<double>( line.get() ) * *multiplier );
}

Result<ViewportResult> EngineContext::renderViewport( FileId id, std::optional<SearchId> searchId,
                                                      LineNumber firstLine, LinesCount visibleLines,
                                                      const ViewportOptions& options ) const
{
    KLOGG_TRACE_SCOPE( "engine", "EngineContext::renderViewport" );

    ViewportResult result;
    const auto lineCount = static_cast<size_t>( std::max<LinesCount::UnderlyingType>(
        visibleLines.get(), static_cast<LinesCount::UnderlyingType>( 0 ) ) );
    if ( lineCount == 0 ) {
        return result;
    }

    auto lines = getLines( id, firstLine, visibleLines, AnsiMode::Raw );
    if ( !lines ) {
        return tl::make_unexpected( lines.error() );
    }

    auto flags = lineFlags( id, searchId, firstLine, visibleLines );
    if ( !flags ) {
        return tl::make_unexpected( flags.error() );
    }

    const auto highlightState = std::atomic_load( &paintHighlights );

    klogg::render::FrameInput frameInput;
    frameInput.lines.reserve( lineCount );
    for ( auto& line : *lines ) {
        frameInput.lines.push_back( std::move( line.text ) );
    }
    // A short answer (a frame running past a truncated file) is padded so the
    // consumer keeps one entry per row it asked for
    frameInput.lines.resize( lineCount );

    frameInput.lineFlags = std::move( *flags );
    frameInput.lineFlags.resize( lineCount, 0 );

    frameInput.firstLine = firstLine;
    frameInput.firstDisplayLineNumber = options.firstDisplayLineNumber;
    frameInput.searchStartIndex = options.searchStartIndex;
    frameInput.searchEndIndex = options.searchEndIndex;
    frameInput.highlighterSet = highlightState ? &highlightState->activeSet : nullptr;
    frameInput.ansiProcessing = options.ansiProcessing;
    frameInput.patternHighlight = options.patternHighlight;
    frameInput.quickFind = options.quickFind;
    frameInput.quickHighlighters
        = quickHighlighterSlots( highlightState, options.quickHighlighterWords );
    frameInput.selectionPortions = options.selectionPortions;
    frameInput.wrapColumns = options.wrapColumns;
    frameInput.firstColumn = options.firstColumn;
    frameInput.visibleColumns = options.visibleColumns;
    frameInput.palette = options.palette;

    auto frame = klogg::render::composeFrame( std::move( frameInput ) );

    result.lines.reserve( frame.lines.size() );
    for ( auto& line : frame.lines ) {
        ViewportLine viewportLine;
        viewportLine.text = std::move( line.displayText );
        viewportLine.parts = std::move( line.parts );
        viewportLine.chunks = std::move( line.chunks );
        viewportLine.lineNumber = line.lineNumber;
        viewportLine.displayLineNumber = line.displayLineNumber;
        viewportLine.flags = line.flagBits;
        viewportLine.foreColor = line.foreColor;
        viewportLine.backColor = line.backColor;
        result.lines.push_back( std::move( viewportLine ) );
    }

    return result;
}

Result<RenderedHtml> EngineContext::renderLinesHtml( FileId id, std::optional<SearchId> searchId,
                                                     const std::vector<LineNumber>& lines,
                                                     const HtmlRenderOptions& options ) const
{
    KLOGG_TRACE_SCOPE( "engine", "EngineContext::renderLinesHtml" );

    // The copied form has no gutter, so the search only exists for parity with
    // renderViewport; resolving it here would add a failure mode the copy has
    // no use for
    (void)searchId;

    if ( lines.empty() ) {
        return serializeFrameAsHtml( klogg::render::Frame{} );
    }

    const auto fileEntry = findFile( id );
    if ( !fileEntry || !fileEntry->logData ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }
    const auto nbLines = fileEntry->logData->getNbLine().get();

    auto fetched = getLinesBatch( id, lines, AnsiMode::Raw );
    if ( !fetched ) {
        return tl::make_unexpected( fetched.error() );
    }

    const auto highlightState = std::atomic_load( &paintHighlights );

    klogg::render::FrameInput frameInput;
    frameInput.lines.reserve( lines.size() );
    for ( std::size_t i = 0; i < lines.size(); ++i ) {
        // A line past the end of the file (the file shrank since the copy was
        // requested) contributes an empty row instead of the engine's padding
        const bool inFile = lines[ i ].get() < nbLines;
        frameInput.lines.push_back(
            inFile && i < fetched->size() ? std::move( ( *fetched )[ i ].text ) : std::string{} );
    }

    frameInput.viewLines = lines;
    frameInput.firstLine = lines.front();
    frameInput.firstDisplayLineNumber = 1_lnum;
    frameInput.searchStartIndex = options.searchStartIndex;
    frameInput.searchEndIndex = options.searchEndIndex;
    frameInput.highlighterSet
        = options.includeHighlighterSet && highlightState ? &highlightState->activeSet : nullptr;
    frameInput.ansiProcessing = options.ansiProcessing;
    frameInput.patternHighlight = options.patternHighlight;
    frameInput.quickHighlighters
        = quickHighlighterSlots( highlightState, options.quickHighlighterWords );
    // No wrapping and no horizontal window: the copy carries every logical line
    // whole
    frameInput.wrapColumns = 0_lcol;
    frameInput.firstColumn = 0_lcol;
    frameInput.visibleColumns = kFullLineWindow;
    frameInput.palette = options.palette;

    return serializeFrameAsHtml( klogg::render::composeFrame( std::move( frameInput ) ) );
}

EngineContext::EngineContext()
{
    // The core no longer reads the TOML: the shell loads the persisted
    // collection, merges its active sets and pushes the flat effective rules
    // through setHighlighters. The default palette is still created here so a
    // headless API session has usable colour labels out of the box.
    quickHighlighters = defaultQuickHighlighters();
}

// ====== Highlight rules ======

Result<void> EngineContext::setHighlighters( const std::vector<HighlightRule>& rules )
{
    klogg::vector<Highlighter> highlighters;
    highlighters.reserve( rules.size() );
    for ( const auto& apiRule : rules ) {
        highlighters.push_back( highlighterFromApiRule( apiRule ) );
    }

    activeHighlighters.setHighlighters( highlighters );
    return {};
}

EngineContext::PaintHighlightState EngineContext::paintHighlightState() const
{
    PaintHighlightState state;
    state.activeSet = activeHighlighters;
    state.quickHighlighters = quickHighlighters;

    // Compile on this side of the publication: the paint path must never be
    // the first one to touch the lazy mutable compilations of the rules. The
    // per-rule compile does the regexp, the throwaway matchLine warms the
    // capture-count cache that matchLine would otherwise write on the reader's
    // thread.
    state.activeSet.compile();
    for ( const auto& rule : state.activeSet.highlighters() ) {
        rule.compile();
        klogg::vector<HighlightedMatch> warmUp;
        rule.matchLine( std::string_view{}, warmUp );
    }

    return state;
}

// ====== Quick highlighters ======

Result<void> EngineContext::setQuickHighlighters( const std::vector<QuickHighlighterDef>& defs )
{
    quickHighlighters.clear();
    quickHighlighters.reserve( defs.size() );
    for ( const auto& def : defs ) {
        quickHighlighters.push_back( quickHighlighterFromApiDef( def ) );
    }
    return {};
}

// ====== Event visualization log types ======

Result<std::vector<LogTypeConfig>> EngineContext::listLogTypeConfigs() const
{
    const auto& logTypes = eventVisualizationManager.getLogTypes();
    std::vector<LogTypeConfig> result;
    result.reserve( logTypes.size() );
    for ( const auto& logType : logTypes ) {
        result.push_back( apiLogTypeFromNative( logType ) );
    }
    return result;
}

Result<void> EngineContext::replaceLogTypeConfigs( const std::vector<LogTypeConfig>& configs )
{
    std::vector<::LogTypeConfig> nativeConfigs;
    nativeConfigs.reserve( configs.size() );
    for ( const auto& config : configs ) {
        nativeConfigs.push_back( nativeLogTypeFromApi( config ) );
    }
    eventVisualizationManager.setLogTypes( nativeConfigs );
    return {};
}

std::optional<::LogTypeConfig> EngineContext::findLogTypeConfigByName( const std::string& name ) const
{
    for ( const auto& logType : eventVisualizationManager.getLogTypes() ) {
        if ( logType.name == name ) {
            return logType;
        }
    }
    return std::nullopt;
}

EventVisualizationManager& EngineContext::eventVisualization()
{
    return eventVisualizationManager;
}

// ====== Session persistence ======

std::string EngineContext::saveSession( const std::vector<FileState>& files ) const
{
    nlohmann::json filesArray = nlohmann::json::array();
    for ( const auto& file : files ) {
        nlohmann::json marksArray = nlohmann::json::array();
        for ( const auto mark : file.marks ) {
            marksArray.push_back( mark );
        }
        filesArray.push_back( nlohmann::json{
            { "path", file.path }, { "encoding", file.encoding }, { "marks", marksArray } } );
    }

    // The session stores the flat effective rules the core owns; named sets,
    // their active flags and the palette are UI-only data it never sees
    nlohmann::json highlightersArray = nlohmann::json::array();
    for ( const auto& rule : activeHighlighters.highlighters() ) {
        const auto apiRule = apiRuleFromHighlighter( rule );
        highlightersArray.push_back( nlohmann::json{
            { "name", apiRule.name },
            { "pattern", apiRule.pattern },
            { "color", apiRule.color },
            { "backColor", apiRule.backColor },
            { "useRegex", apiRule.useRegex },
            { "caseSensitive", apiRule.caseSensitive },
            { "useThemeBack", apiRule.useThemeBack },
            { "onlyMatch", apiRule.onlyMatch },
            { "variateColors", apiRule.variateColors },
            { "colorVariance", apiRule.colorVariance },
            { "enabled", apiRule.enabled } } );
    }

    nlohmann::json root{
        { "version", 1 }, { "files", filesArray }, { "highlighters", highlightersArray } };

    // Compact with the invalid-UTF-8 bytes replaced: a path that is not valid
    // UTF-8 must not abort the save, and the replacement matches what the
    // QString round trip used to produce
    return root.dump( -1, ' ', false, nlohmann::json::error_handler_t::replace );
}

Result<std::vector<EngineContext::FileState>>
EngineContext::restoreSession( const std::string& json )
{
    nlohmann::json root;
    try {
        root = nlohmann::json::parse( json );
    }
    catch ( const nlohmann::json::parse_error& error ) {
        return tl::make_unexpected( Error{ ErrorCode::InternalError,
                                           std::string{ "invalid session json: " }
                                               + error.what() } );
    }
    if ( !root.is_object() ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InternalError, "invalid session json: not an object" } );
    }

    // The flat effective rules are the current shape; a legacy session file
    // still carries named sets, in which case the rules of the active ones are
    // flattened (only active highlights ever reached the render path)
    const auto parseRule = []( const nlohmann::json& ruleObject ) {
        // Missing keys keep the Highlighter defaults (old session files)
        HighlightRule apiRule{};
        apiRule.name = jsonString( ruleObject, "name" );
        apiRule.pattern = jsonString( ruleObject, "pattern" );
        apiRule.color = jsonString( ruleObject, "color" );
        apiRule.backColor = jsonString( ruleObject, "backColor" );
        apiRule.useRegex = jsonBool( ruleObject, "useRegex", true );
        apiRule.caseSensitive = jsonBool( ruleObject, "caseSensitive", true );
        apiRule.useThemeBack = jsonBool( ruleObject, "useThemeBack", true );
        apiRule.onlyMatch = jsonBool( ruleObject, "onlyMatch", false );
        apiRule.variateColors = jsonBool( ruleObject, "variateColors", false );
        apiRule.colorVariance = jsonInt( ruleObject, "colorVariance", 15 );
        apiRule.enabled = jsonBool( ruleObject, "enabled", true );
        return highlighterFromApiRule( apiRule );
    };

    if ( root.contains( "highlighters" ) ) {
        klogg::vector<Highlighter> rules;
        for ( const auto& ruleValue : jsonArray( root, "highlighters" ) ) {
            rules.push_back( parseRule( jsonObject( ruleValue ) ) );
        }
        activeHighlighters.setHighlighters( rules );
    }
    else if ( root.contains( "highlightSets" ) ) {
        klogg::vector<Highlighter> rules;
        for ( const auto& setValue : jsonArray( root, "highlightSets" ) ) {
            const auto setObject = jsonObject( setValue );
            if ( !jsonBool( setObject, "active", false ) ) {
                continue;
            }
            for ( const auto& ruleValue : jsonArray( setObject, "rules" ) ) {
                rules.push_back( parseRule( jsonObject( ruleValue ) ) );
            }
        }
        activeHighlighters.setHighlighters( rules );
    }

    std::vector<FileState> files;
    const auto filesArray = jsonArray( root, "files" );
    files.reserve( static_cast<size_t>( filesArray.size() ) );
    for ( const auto& fileValue : filesArray ) {
        const auto fileObject = jsonObject( fileValue );
        FileState file;
        file.path = jsonString( fileObject, "path" );
        file.encoding = jsonString( fileObject, "encoding" );
        const auto marksArray = jsonArray( fileObject, "marks" );
        file.marks.reserve( static_cast<size_t>( marksArray.size() ) );
        for ( const auto& markValue : marksArray ) {
            // Anything that is not an integer reads as mark 0 (the old
            // conversion returned the default for those entries)
            file.marks.push_back( markValue.is_number_integer()
                                      ? static_cast<uint64_t>( markValue.get<int64_t>() )
                                      : uint64_t{ 0 } );
        }
        files.push_back( std::move( file ) );
    }
    return files;
}

void EngineContext::publishPaintHighlights()
{
    auto state = std::make_shared<PaintHighlightState>( paintHighlightState() );
    std::atomic_store(
        &paintHighlights, std::shared_ptr<const PaintHighlightState>( std::move( state ) ) );
}

Result<void> EngineContext::cancelSearchOnWorker( SearchId id )
{
    const auto [ fileEntry, entry ] = findSearchEntry( id );
    if ( !entry ) {
        // Unknown id is a no-op: cancel is idempotent
        return {};
    }
    std::shared_ptr<LogFilteredData> filteredData;
    {
        std::lock_guard lock( entry->stateMutex );
        if ( entry->cancelled ) {
            return {};
        }
        entry->cancelled = true;
        // The reference is moved out under the lock so a concurrent read
        // either resolves the live data or sees SearchCancelled
        filteredData = std::move( entry->filteredData );
    }
    // The actual destruction is deferred to the executor queue by the custom
    // deleter; interrupting first keeps the wind-down prompt
    filteredData->interruptSearch();
    filteredData.reset();
    return {};
}

Result<void> EngineContext::interruptSearchOnWorker( SearchId id )
{
    const auto [ fileEntry, entry ] = findSearchEntry( id );
    if ( !entry ) {
        // Unknown id is a no-op, same as cancel
        return {};
    }

    std::shared_ptr<LogFilteredData> filteredData;
    {
        std::lock_guard lock( entry->stateMutex );
        if ( entry->cancelled ) {
            return {};
        }
        filteredData = entry->filteredData;
    }
    // Stops the worker but keeps the entry readable: a stopped search keeps
    // rendering its partial results (the UI Stop action relies on this)
    filteredData->interruptSearch();
    return {};
}

void EngineContext::addProgressCallbackOnWorker( SearchId id, ProgressCallback cb )
{
    const auto [ fileEntry, entry ] = findSearchEntry( id );
    if ( !entry ) {
        // Unknown id is ignored by design
        return;
    }
    entry->callbacks.push_back( std::move( cb ) );
}

Result<ExtractionId> EngineContext::extractEventsOnWorker( FileId fileId, ExtractionId extractionId,
                                                           const ExtractionParams& params )
{
    const auto fileEntry = findFile( fileId );
    if ( !fileEntry || !fileEntry->logData ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( fileId ) } );
    }

    const auto config = findLogTypeConfigByName( params.logTypeConfigName );
    if ( !config ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InternalError,
                   "log type not found: " + params.logTypeConfigName } );
    }

    // One combined pattern drives the engine search (same approach as the Qt
    // UI); enabled rules without any usable pattern are skipped
    std::vector<std::string> patterns;
    std::vector<::EventRule> rules;
    for ( const auto& rule : config->getAllEventRules() ) {
        if ( !rule.enabled ) {
            continue;
        }
        std::vector<std::string> rulePatterns;
        if ( rule.eventType == ::EventType::Instance ) {
            if ( !rule.pattern.empty() ) {
                rulePatterns.push_back( rule.pattern );
            }
        }
        else {
            if ( !rule.beginPattern.empty() ) {
                rulePatterns.push_back( rule.beginPattern );
            }
            if ( !rule.endPattern.empty() ) {
                rulePatterns.push_back( rule.endPattern );
            }
        }
        if ( rulePatterns.empty() ) {
            continue;
        }
        for ( const auto& rulePattern : rulePatterns ) {
            patterns.push_back( "(?:" + flattenNamedGroups( rulePattern ) + ")" );
        }
        rules.push_back( rule );
    }
    if ( patterns.empty() ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InvalidPattern, "no enabled event rules with patterns" } );
    }

    std::string combinedPattern;
    for ( const auto& rulePattern : patterns ) {
        if ( !combinedPattern.empty() ) {
            combinedPattern.push_back( '|' );
        }
        combinedPattern += rulePattern;
    }

    auto* logData = fileEntry->logData.get();
    auto filteredData
        = wrapFilteredData( logData->getNewFilteredData(), executor, fileEntry->logData );
    filteredData->setVisibility( LogFilteredData::VisibilityFlags::Events );

    ExtractionEntry* entry = nullptr;
    {
        UniqueLock lock( fileEntry->searchesMutex_ );
        entry = &fileEntry->extractions.try_emplace( extractionId ).first->second;
        entry->filteredData = std::move( filteredData );
        entry->logType = *config;
        entry->rules = rules;
        entry->startTime = std::chrono::steady_clock::now();
        entry->startLine = params.fromLine.value_or( 0_lnum );
        // Engine endLine is exclusive (one past the last line to scan), same
        // semantics as the search API
        entry->endLine = params.toLine.value_or( LineNumber( logData->getNbLine().get() ) );
    }
    // The node pointer stays valid: extractions are never individually erased
    // and the file entry is held for the whole call

    entry->filteredData->addEventsSearchProgressedListener(
        [ this, fileId, extractionId ]( LinesCount nbEvents, int progress, LineNumber ) {
            onExtractionProgressed( fileId, extractionId, nbEvents, progress );
        } );

    const RegularExpressionPattern pattern( combinedPattern, false /* caseSensitive */,
                                            false /* inverse */, false /* boolean */,
                                            false /* plainText */ );
    entry->filteredData->runSearchIntoArray( pattern, LogFilteredData::SearchOutputArray::Events,
                                             entry->startLine, entry->endLine );
    return extractionId;
}

void EngineContext::onExtractionProgressed( FileId fileId, ExtractionId extractionId,
                                            LinesCount nbEvents, int progress )
{
    const auto [ fileEntry, extractionEntry ] = findExtractionEntry( extractionId );
    if ( !fileEntry || fileEntry->info.id != fileId ) {
        return;
    }
    auto& entry = *extractionEntry;
    const auto rangeLines = LinesCount( entry.endLine.get() - entry.startLine.get() );

    {
        std::lock_guard lock( entry.stateMutex );
        if ( entry.cancelled ) {
            return;
        }
        entry.progressPercent = progress;
        entry.eventsFound = static_cast<uint32_t>( nbEvents.get() );
        // The engine exposes no processed-lines counter; approximate it from
        // the progress percentage over the scanned range
        entry.processedLines
            = LinesCount( rangeLines.get() * static_cast<uint64_t>( progress ) / 100 );
    }

    const auto ratio = std::clamp( static_cast<float>( progress ) / 100.0f, 0.0f, 1.0f );
    emitEvent( "event.extraction.progress",
               "{\"fileId\":" + std::to_string( fileId ) + ",\"extractionId\":"
                   + std::to_string( extractionId ) + ",\"progress\":" + std::to_string( ratio )
                   + ",\"eventsFound\":" + std::to_string( nbEvents.get() ) + "}" );

    auto justCompleted = false;
    {
        std::lock_guard lock( entry.stateMutex );
        if ( !entry.completed && progress >= 100 ) {
            entry.completed = true;
            justCompleted = true;
        }
    }
    if ( !justCompleted ) {
        return;
    }

    // The events search only reports matching lines; re-match them against
    // the individual rule patterns to recover the named parameters
    auto* logData = fileEntry->logData.get();
    const auto filteredEventLines = entry.filteredData->getEvents();
    std::vector<LineNumber> eventLines( filteredEventLines.cbegin(), filteredEventLines.cend() );
    const auto lastLine = entry.endLine.get() > 0 ? LineNumber( entry.endLine.get() - 1 ) : 0_lnum;
    entry.events = eventVisualization().extractEventsFromSearchResults(
        logData, eventLines, entry.rules, &entry.logType.parsingRule );
    entry.ranges = eventVisualization().buildSliceRanges( entry.events, lastLine );

    // This extraction becomes the file's source for the LineFlagEvent gutter
    // bit and the events half of the searches' union projections
    {
        UniqueLock lock( fileEntry->fieldsMutex_ );
        fileEntry->eventLines.clear();
        for ( const auto& event : entry.events ) {
            fileEntry->eventLines.insert( event.line );
        }
        fileEntry->eventLinesSource = extractionId;
    }
    syncEventLinesToSearches( *fileEntry );

    const auto eventCount = static_cast<uint32_t>( entry.events.size() );
    {
        std::lock_guard lock( entry.stateMutex );
        entry.eventsFound = eventCount;
        entry.processedLines = rangeLines;
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - entry.startTime )
                                .count();
    emitEvent( "event.extraction.completed",
               "{\"fileId\":" + std::to_string( fileId )
                   + ",\"extractionId\":" + std::to_string( extractionId )
                   + ",\"eventCount\":" + std::to_string( eventCount )
                   + ",\"durationMs\":" + std::to_string( durationMs ) + "}" );
}

Result<ExtractionProgress> EngineContext::extractionProgressOnWorker( ExtractionId id )
{
    const auto [ fileEntry, entry ] = findExtractionEntry( id );
    if ( !entry ) {
        return tl::make_unexpected( Error{ ErrorCode::InvalidExtractionId,
                                           "unknown extraction id: " + std::to_string( id ) } );
    }

    std::lock_guard lock( entry->stateMutex );
    if ( entry->cancelled ) {
        return tl::make_unexpected( Error{ ErrorCode::ExtractionCancelled,
                                           "extraction cancelled: " + std::to_string( id ) } );
    }

    ExtractionProgress result;
    result.id = id;
    result.processedLines = entry->processedLines;
    result.eventsFound = entry->eventsFound;
    result.progress
        = std::clamp( static_cast<float>( entry->progressPercent ) / 100.0f, 0.0f, 1.0f );
    return result;
}

Result<std::vector<ExtractedEvent>>
EngineContext::extractedEventsOnWorker( ExtractionId id, uint64_t from, uint32_t count )
{
    const auto [ fileEntry, entry ] = findExtractionEntry( id );
    if ( !entry ) {
        return tl::make_unexpected( Error{ ErrorCode::InvalidExtractionId,
                                           "unknown extraction id: " + std::to_string( id ) } );
    }
    {
        std::lock_guard lock( entry->stateMutex );
        if ( entry->cancelled ) {
            return tl::make_unexpected( Error{ ErrorCode::ExtractionCancelled,
                                               "extraction cancelled: " + std::to_string( id ) } );
        }
    }

    // Events are populated at completion; a running extraction reads as empty
    std::vector<ExtractedEvent> result;
    const auto total = static_cast<uint64_t>( entry->events.size() );
    if ( from >= total || count == 0 ) {
        return result;
    }

    const auto end = std::min( from + count, total );
    result.reserve( end - from );
    for ( auto index = from; index < end; ++index ) {
        result.push_back( apiEventFromNative( entry->events[ index ] ) );
    }
    return result;
}

Result<void> EngineContext::cancelExtractionOnWorker( ExtractionId id )
{
    const auto [ owningFile, entry ] = findExtractionEntry( id );
    if ( !entry ) {
        // Unknown id is a no-op: cancel is idempotent
        return {};
    }

    std::shared_ptr<LogFilteredData> filteredData;
    {
        std::lock_guard lock( entry->stateMutex );
        if ( entry->cancelled ) {
            return {};
        }
        entry->cancelled = true;
        // Unlike searches this filtered data is session-only, so it can be
        // released immediately; the cancelled flag keeps late queries at
        // ExtractionCancelled
        filteredData = std::move( entry->filteredData );
    }
    if ( filteredData ) {
        filteredData->interruptSearch();
        filteredData.reset();
    }
    // A cancelled extraction no longer backs anything: free its results
    // and drop the gutter bit source if it provided one
    entry->events.clear();
    entry->ranges.clear();
    bool droppedSource = false;
    {
        UniqueLock lock( owningFile->fieldsMutex_ );
        if ( owningFile->eventLinesSource == id ) {
            owningFile->eventLines.clear();
            owningFile->eventLinesSource.reset();
            droppedSource = true;
        }
    }
    if ( droppedSource ) {
        syncEventLinesToSearches( *owningFile );
    }
    return {};
}

Result<std::vector<SliceRange>> EngineContext::sliceRangesOnWorker( ExtractionId id )
{
    const auto [ fileEntry, entry ] = findExtractionEntry( id );
    if ( !entry ) {
        return tl::make_unexpected( Error{ ErrorCode::InvalidExtractionId,
                                           "unknown extraction id: " + std::to_string( id ) } );
    }
    {
        std::lock_guard lock( entry->stateMutex );
        if ( entry->cancelled ) {
            return tl::make_unexpected( Error{ ErrorCode::ExtractionCancelled,
                                               "extraction cancelled: " + std::to_string( id ) } );
        }
    }

    std::vector<SliceRange> result;
    result.reserve( static_cast<size_t>( entry->ranges.size() ) );
    for ( const auto& range : entry->ranges ) {
        result.push_back( apiSliceRangeFromNative( range ) );
    }
    return result;
}

Result<void> EngineContext::setEventCategoryFilterOnWorker( FileId id,
                                                            std::vector<std::string> categories )
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }

    {
        UniqueLock lock( fileEntry->fieldsMutex_ );
        fileEntry->eventCategoryFilter
            = std::set<std::string>( categories.begin(), categories.end() );
    }
    syncEventLinesToSearches( *fileEntry );
    return {};
}

Result<EventExportResult>
EngineContext::exportEventsOnWorker( ExtractionId id, const std::string& outputPath,
                                     ExportFormat format,
                                     const std::optional<std::vector<std::string>>& categories )
{
    if ( format == ExportFormat::BinaryProto ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InternalError, "binary proto export is not implemented" } );
    }

    auto [ owningFile, entry ] = findExtractionEntry( id );
    if ( !entry ) {
        return tl::make_unexpected( Error{ ErrorCode::InvalidExtractionId,
                                           "unknown extraction id: " + std::to_string( id ) } );
    }
    {
        std::lock_guard lock( entry->stateMutex );
        if ( entry->cancelled ) {
            return tl::make_unexpected( Error{ ErrorCode::ExtractionCancelled,
                                               "extraction cancelled: " + std::to_string( id ) } );
        }
        if ( !entry->completed ) {
            return tl::make_unexpected(
                Error{ ErrorCode::InternalError, "extraction still in progress" } );
        }
    }

    std::optional<std::set<std::string>> categoryFilter;
    if ( categories ) {
        categoryFilter.emplace( categories->begin(), categories->end() );
    }

    // Statistics follow the native exportToPerfetto semantics: skipped events
    // lack a valid timestamp; orphan counts come from the converted phases.
    // All counts cover only the events passing the category filter.
    std::vector<::LogEvent> validEvents;
    int skippedCount = 0;
    for ( const auto& event : entry->events ) {
        if ( categoryFilter && categoryFilter->count( event.category ) == 0 ) {
            continue;
        }
        if ( event.hasValidTimestamp ) {
            validEvents.push_back( event );
        }
        else {
            ++skippedCount;
        }
    }
    if ( validEvents.empty() ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InternalError, "no events with valid timestamps" } );
    }

    const auto perfettoEvents
        = eventVisualization().convertToPerfettoFormat( validEvents );
    if ( perfettoEvents.empty() ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InternalError, "no events could be exported" } );
    }

    const JsonExporter exporter;
    if ( !exporter.exportToFile( outputPath, perfettoEvents ) ) {
        return tl::make_unexpected(
            Error{ ErrorCode::InternalError, "could not write export file: " + outputPath } );
    }

    EventExportResult result;
    result.outputPath = outputPath;
    result.eventCount = static_cast<int>( perfettoEvents.size() );
    result.skippedCount = skippedCount;
    for ( const auto& perfEvent : perfettoEvents ) {
        if ( perfEvent.ph == "X" ) {
            ++result.completeEventCount;
        }
        else if ( perfEvent.ph == "B" ) {
            ++result.orphanBeginCount;
        }
        else if ( perfEvent.ph == "E" ) {
            ++result.orphanEndCount;
        }
    }

    // Same probe as the native export: PATH first, then next to the executable
    auto traceProcessor = platform::paths::findExecutable( "trace_processor_shell" );
    if ( traceProcessor.empty() ) {
        auto bundled = platform::paths::executableDir() / "trace_processor_shell";
#if defined( _WIN32 )
        bundled += ".exe";
#endif
        std::error_code error;
        if ( std::filesystem::exists( bundled, error ) ) {
            traceProcessor = bundled;
        }
    }
    result.hasTraceProcessor = !traceProcessor.empty();
    return result;
}

Result<std::vector<LineNumber>> EngineContext::listMarks( FileId id ) const
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }
    SharedLock lock( fileEntry->fieldsMutex_ );
    // std::set keeps them in ascending order
    return std::vector<LineNumber>( fileEntry->marks.begin(), fileEntry->marks.end() );
}

Result<void> EngineContext::addMarksOnWorker( FileId id, const std::vector<LineNumber>& lines )
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }
    {
        UniqueLock lock( fileEntry->fieldsMutex_ );
        fileEntry->marks.insert( lines.begin(), lines.end() );
    }
    // Existing projections with Marks visible take the change right away; the
    // fields lock is released first because the mirror reads the searches
    syncMarkLinesToSearches( *fileEntry, lines, true /* added */ );
    // Other consumers of this file (e.g. a second window) track the marks in
    // their own caches and need a nudge to re-read them; emitted outside the
    // lock: handlers may call back into the read channel
    emitEvent( "marks.changed", "{\"fileId\":" + std::to_string( id ) + "}" );
    return {};
}

Result<void> EngineContext::removeMarksOnWorker( FileId id, const std::vector<LineNumber>& lines )
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }
    {
        UniqueLock lock( fileEntry->fieldsMutex_ );
        for ( const auto line : lines ) {
            fileEntry->marks.erase( line );
        }
    }
    // Same incremental mirror as addMarksOnWorker, for the projections that
    // show marks
    syncMarkLinesToSearches( *fileEntry, lines, false /* added */ );
    emitEvent( "marks.changed", "{\"fileId\":" + std::to_string( id ) + "}" );
    return {};
}

Result<std::optional<LineNumber>> EngineContext::markAfter( FileId id, LineNumber line ) const
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }
    SharedLock lock( fileEntry->fieldsMutex_ );
    const auto& marks = fileEntry->marks;
    const auto mark = marks.upper_bound( line );
    if ( mark == marks.end() ) {
        return std::optional<LineNumber>{};
    }
    return *mark;
}

Result<std::optional<LineNumber>> EngineContext::markBefore( FileId id, LineNumber line ) const
{
    const auto fileEntry = findFile( id );
    if ( !fileEntry ) {
        return tl::make_unexpected(
            Error{ ErrorCode::FileNotOpen, "unknown file id: " + std::to_string( id ) } );
    }
    SharedLock lock( fileEntry->fieldsMutex_ );
    const auto& marks = fileEntry->marks;
    const auto mark = marks.lower_bound( line );
    if ( mark == marks.begin() ) {
        return std::optional<LineNumber>{};
    }
    return *std::prev( mark );
}

Result<std::string> EngineContext::saveSessionOnWorker() const
{
    std::vector<FileState> files;
    {
        SharedLock lock( entriesMutex_ );
        files.reserve( entries.size() );
        for ( const auto& [ id, entry ] : entries ) {
            const auto info = snapshotInfo( *entry );
            FileState file;
            file.path = info.path;
            file.encoding = info.encoding.name;
            {
                SharedLock fieldsLock( entry->fieldsMutex_ );
                file.marks.reserve( entry->marks.size() );
                for ( const auto mark : entry->marks ) {
                    file.marks.push_back( mark.get() );
                }
            }
            files.push_back( std::move( file ) );
        }
    }
    return saveSession( files );
}

void EngineContext::destroyAllOnWorker()
{
    // Readers may still hold entry references; the actual destruction is
    // posted back to this executor by the custom deleters (searches first)
    std::map<FileId, std::shared_ptr<FileEntry>> drained;
    {
        UniqueLock lock( entriesMutex_ );
        drained.swap( entries );
    }
    for ( auto& [ fileId, fileEntry ] : drained ) {
        SharedLock searchesLock( fileEntry->searchesMutex_ );
        for ( auto& [ searchId, search ] : fileEntry->searches ) {
            if ( search.filteredData ) {
                search.filteredData->interruptSearch();
            }
        }
        for ( auto& [ extractionId, extraction ] : fileEntry->extractions ) {
            if ( extraction.filteredData ) {
                extraction.filteredData->interruptSearch();
            }
        }
    }
    // `drained` destructs at scope end; anything not held by readers posts its
    // teardown to this executor (drained by the stop() that follows)
}

void EngineContext::emitEvent( const std::string& eventType, const std::string& json )
{
    if ( eventSink ) {
        eventSink( eventType, json );
    }
}

Session::Impl::Impl( EngineOptions options )
{
    worker = std::make_shared<SessionExecutor>( "klogg-api-worker" );
    context = new EngineContext();
    // The paint path reads the highlight state of the session through this
    // published snapshot; the initial one is built here, before the session is
    // handed out, and every later mutation republishes it from the worker
    context->publishPaintHighlights();
    context->executor = worker;
    context->optionsHolder = std::make_shared<EngineOptionsHolder>();
    context->optionsHolder->set( std::move( options ) );
    context->eventSink = [ this ]( const std::string& eventType, const std::string& json ) {
        emitEvent( eventType, json );
    };
}

Session::Impl::~Impl()
{
    if ( worker->isExecutorThread() ) {
        // Destroying a session from one of its own callbacks is not supported:
        // stop() would join the calling thread and throw out of this
        // destructor (std::terminate). Release what is reachable from here and
        // leak the rest �?the engine context and its worker stay alive (the
        // context holds a reference to the executor, so nothing ever destroys
        // it from its own thread), which is the far lesser evil.
        LOG_ERROR << "Session destroyed from its own worker thread: skipping teardown";
        // The leaked context keeps running and would call into the destroyed
        // Impl through this sink; the assignment is safe here, the teardown
        // tasks below are what cannot run
        context->eventSink = nullptr;
        return;
    }

    // Stop event delivery first: subscribers may already be gone by the time
    // engine teardown emits its final signals
    dispatch( []( EngineContext* ctx ) { ctx->eventSink = nullptr; } );
    dispatch( []( EngineContext* ctx ) { ctx->destroyAllOnWorker(); } );
    // Drains the queue and joins the worker thread, like quit()+wait() did
    worker->stop();
    // Safe once the worker thread has stopped
    delete context;
}

void Session::Impl::emitEvent( const std::string& eventType, const std::string& json ) const
{
    std::vector<EventHandler> snapshot;
    {
        std::lock_guard lock( handlersMutex );
        snapshot = handlers;
    }
    for ( const auto& handler : snapshot ) {
        handler( eventType, json );
    }
}

} // namespace klogg::api
