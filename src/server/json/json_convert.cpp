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

#include "json_convert.h"

#include <optional>
#include <stdexcept>

namespace klogg::server {

using nlohmann::json;

namespace {

constexpr Rgba kDefaultText( 0xcd, 0xd6, 0xf4 );
constexpr Rgba kDefaultBase( 0x1e, 0x1e, 0x2e );
constexpr Rgba kDefaultDimmedText( 0x6c, 0x70, 0x86 );
constexpr Rgba kDefaultSelectionText( 0x1e, 0x1e, 0x2e );
constexpr Rgba kDefaultSelectionBack( 0x89, 0xb4, 0xfa );

Rgba parseColor( const json& object, const char* key, Rgba fallback )
{
    const auto it = object.find( key );
    if ( it == object.end() || it->is_null() ) {
        return fallback;
    }
    const auto text = it->get<std::string>();
    if ( text.empty() ) {
        return Rgba{};
    }
    const auto parsed = Rgba::fromHex( text );
    if ( !parsed ) {
        throw std::invalid_argument( std::string( "invalid color in " ) + key );
    }
    return *parsed;
}

std::optional<LineNumber> optionalLine( const json& object, const char* key )
{
    const auto it = object.find( key );
    if ( it == object.end() || it->is_null() ) {
        return std::nullopt;
    }
    return LineNumber( it->get<uint64_t>() );
}

AnsiProcessing parseAnsiProcessing( const json& object )
{
    const auto text = object.value( "ansiProcessing", std::string( "none" ) );
    if ( text == "none" ) {
        return AnsiProcessing::None;
    }
    if ( text == "strip" || text == "stripOnly" ) {
        return AnsiProcessing::StripOnly;
    }
    if ( text == "renderColors" ) {
        return AnsiProcessing::RenderColors;
    }
    throw std::invalid_argument( "unknown ansiProcessing: " + text );
}

render::FramePalette parsePalette( const json& source )
{
    if ( !source.contains( "palette" ) ) {
        return { kDefaultText, kDefaultBase, kDefaultDimmedText, kDefaultSelectionText,
                 kDefaultSelectionBack };
    }

    const auto& palette = source[ "palette" ];
    render::FramePalette result;
    result.text = parseColor( palette, "text", kDefaultText );
    result.base = parseColor( palette, "base", kDefaultBase );
    result.dimmedText = parseColor( palette, "dimmedText", kDefaultDimmedText );
    result.selectionText = parseColor( palette, "selectionText", kDefaultSelectionText );
    result.selectionBack = parseColor( palette, "selectionBack", kDefaultSelectionBack );
    return result;
}

std::optional<render::PatternHighlightOptions> parsePatternHighlight( const json& source )
{
    if ( !source.contains( "patternHighlight" ) || source[ "patternHighlight" ].is_null() ) {
        return std::nullopt;
    }

    const auto& item = source[ "patternHighlight" ];
    render::PatternHighlightOptions options;
    options.pattern = item.at( "pattern" ).get<std::string>();
    options.caseSensitive = item.value( "caseSensitive", false );
    options.useRegex = item.value( "useRegex", true );
    options.variateColors = item.value( "variateColors", false );
    options.foreColor = parseColor( item, "foreColor", Rgba{} );
    options.backColor = parseColor( item, "backColor", Rgba{} );
    return options;
}

std::vector<std::vector<std::string>> parseQuickHighlighterWords( const json& source )
{
    std::vector<std::vector<std::string>> slots;
    if ( !source.contains( "quickHighlighterWords" ) ) {
        return slots;
    }

    for ( const auto& slot : source[ "quickHighlighterWords" ] ) {
        std::vector<std::string> words;
        for ( const auto& word : slot ) {
            words.push_back( word.get<std::string>() );
        }
        slots.push_back( std::move( words ) );
    }
    return slots;
}

} // namespace

json toJson( const api::FileInfo& info )
{
    return {
        { "fileId", info.id },
        { "path", info.path },
        { "sizeBytes", info.size_bytes },
        { "totalLines", info.total_lines.get() },
        { "maxLineLength", info.max_line_length.get() },
        { "encoding",
          { { "name", info.encoding.name }, { "confidence", info.encoding.confidence } } },
        { "lastModifiedMs", info.last_modified_ms },
        { "indexed", info.indexed },
    };
}

json toJson( const api::LineData& line )
{
    json segments = json::array();
    for ( const auto& segment : line.segments ) {
        segments.push_back( {
            { "start", segment.start },
            { "end", segment.end },
            { "fg", segment.fg },
            { "bg", segment.bg },
            { "bold", segment.bold },
            { "italic", segment.italic },
            { "underline", segment.underline },
        } );
    }
    return {
        { "num", line.num.get() },
        { "text", line.text },
        { "segments", std::move( segments ) },
    };
}

json toJson( const api::SearchResult& result )
{
    return {
        { "searchId", result.id },
        { "totalMatches", result.total_matches.get() },
        { "processedLines", result.processed_lines.get() },
        { "progress", result.progress },
    };
}

json toJson( const api::PatternInfo& info )
{
    return {
        { "valid", info.valid },
        { "error", info.error },
        { "isPlainText", info.isPlainText },
    };
}

json toJson( const api::ViewportResult& result )
{
    json lines = json::array();
    for ( const auto& line : result.lines ) {
        json parts = json::array();
        for ( const auto& part : line.parts ) {
            parts.push_back( {
                { "line", part.line.get() },
                { "start", part.start.get() },
                { "length", part.length.get() },
            } );
        }

        json chunks = json::array();
        for ( const auto& chunk : line.chunks ) {
            chunks.push_back( {
                { "start", chunk.start.get() },
                { "length", chunk.length.get() },
                { "foreColor", chunk.foreColor.toHex() },
                { "backColor", chunk.backColor.toHex() },
                { "startsRow", chunk.startsRow },
            } );
        }

        lines.push_back( {
            { "text", line.text },
            { "parts", std::move( parts ) },
            { "chunks", std::move( chunks ) },
            { "lineNumber", line.lineNumber.get() },
            { "displayLineNumber", line.displayLineNumber.get() },
            { "flags", line.flags },
            { "foreColor", line.foreColor.toHex() },
            { "backColor", line.backColor.toHex() },
        } );
    }
    return { { "lines", std::move( lines ) } };
}

json toJson( const api::RenderedHtml& result )
{
    return {
        { "html", result.html },
        { "text", result.text },
    };
}

const char* errorCodeName( api::ErrorCode code )
{
    switch ( code ) {
    case api::ErrorCode::FileNotFound:
        return "FileNotFound";
    case api::ErrorCode::FileNotOpen:
        return "FileNotOpen";
    case api::ErrorCode::EncodingError:
        return "EncodingError";
    case api::ErrorCode::SearchCancelled:
        return "SearchCancelled";
    case api::ErrorCode::InvalidPattern:
        return "InvalidPattern";
    case api::ErrorCode::InvalidFileId:
        return "InvalidFileId";
    case api::ErrorCode::InvalidSearchId:
        return "InvalidSearchId";
    case api::ErrorCode::InvalidExtractionId:
        return "InvalidExtractionId";
    case api::ErrorCode::ExtractionCancelled:
        return "ExtractionCancelled";
    case api::ErrorCode::InternalError:
        return "InternalError";
    }
    return "InternalError";
}

int httpStatus( api::ErrorCode code )
{
    switch ( code ) {
    case api::ErrorCode::FileNotFound:
    case api::ErrorCode::FileNotOpen:
    case api::ErrorCode::InvalidFileId:
    case api::ErrorCode::InvalidSearchId:
    case api::ErrorCode::InvalidExtractionId:
        return 404;
    case api::ErrorCode::InvalidPattern:
    case api::ErrorCode::EncodingError:
        return 400;
    case api::ErrorCode::SearchCancelled:
    case api::ErrorCode::ExtractionCancelled:
        return 409;
    case api::ErrorCode::InternalError:
        break;
    }
    return 500;
}

api::AnsiMode parseAnsiMode( const json& params )
{
    const auto text = params.value( "ansi", std::string( "raw" ) );
    if ( text == "raw" ) {
        return api::AnsiMode::Raw;
    }
    if ( text == "strip" ) {
        return api::AnsiMode::Strip;
    }
    if ( text == "segments" ) {
        return api::AnsiMode::Segments;
    }
    throw std::invalid_argument( "unknown ansi mode: " + text );
}

api::SearchParams parseSearchParams( const json& params )
{
    const auto& source = params.at( "params" );
    api::SearchParams result;
    result.pattern = source.at( "pattern" ).get<std::string>();
    result.caseSensitive = source.value( "caseSensitive", false );
    result.isRegex = source.value( "isRegex", true );
    result.invertMatch = source.value( "invertMatch", false );
    result.isBoolean = source.value( "isBoolean", false );
    result.fromLine = optionalLine( source, "fromLine" );
    result.toLine = optionalLine( source, "toLine" );
    if ( source.contains( "maxResults" ) && !source[ "maxResults" ].is_null() ) {
        result.maxResults = source[ "maxResults" ].get<uint32_t>();
    }
    return result;
}

api::ViewportOptions parseViewportOptions( const json& params )
{
    static const json emptyOptions = json::object();
    const auto& source = params.contains( "options" ) ? params[ "options" ] : emptyOptions;

    api::ViewportOptions result;
    result.wrapColumns = LineColumn( source.value( "wrapColumns", int64_t{ 0 } ) );
    result.firstColumn = LineColumn( source.value( "firstColumn", int64_t{ 0 } ) );
    result.visibleColumns = LineLength( source.value( "visibleColumns", int64_t{ 0 } ) );
    result.searchStartIndex = LineNumber( source.value( "searchStartIndex", uint64_t{ 0 } ) );
    result.searchEndIndex = LineNumber( source.value( "searchEndIndex", uint64_t{ 0 } ) );
    result.firstDisplayLineNumber
        = LineNumber( source.value( "firstDisplayLineNumber", uint64_t{ 1 } ) );
    result.ansiProcessing = parseAnsiProcessing( source );
    result.palette = parsePalette( source );

    if ( source.contains( "selectionPortions" ) ) {
        for ( const auto& item : source[ "selectionPortions" ] ) {
            render::SelectionPortion portion;
            portion.lineIndex = item.value( "lineIndex", std::size_t{ 0 } );
            portion.startColumn = LineColumn( item.value( "startColumn", int64_t{ 0 } ) );
            portion.length = LineLength( item.value( "length", int64_t{ 0 } ) );
            portion.wholeLine = item.value( "wholeLine", false );
            result.selectionPortions.push_back( portion );
        }
    }

    result.patternHighlight = parsePatternHighlight( source );

    if ( source.contains( "quickFind" ) && !source[ "quickFind" ].is_null() ) {
        const auto& item = source[ "quickFind" ];
        render::QuickFindOptions options;
        options.pattern = item.at( "pattern" ).get<std::string>();
        options.caseSensitive = item.value( "caseSensitive", true );
        options.regex = item.value( "regex", true );
        options.backColor = parseColor( item, "backColor", Rgba{} );
        result.quickFind = options;
    }

    result.quickHighlighterWords = parseQuickHighlighterWords( source );

    return result;
}

api::HtmlRenderOptions parseHtmlRenderOptions( const json& params )
{
    static const json emptyOptions = json::object();
    const auto& source = params.contains( "options" ) ? params[ "options" ] : emptyOptions;

    api::HtmlRenderOptions result;
    result.palette = parsePalette( source );
    result.ansiProcessing = parseAnsiProcessing( source );
    result.patternHighlight = parsePatternHighlight( source );
    result.quickHighlighterWords = parseQuickHighlighterWords( source );
    result.searchStartIndex = LineNumber( source.value( "searchStartIndex", uint64_t{ 0 } ) );
    result.searchEndIndex = LineNumber( source.value( "searchEndIndex", uint64_t{ 0 } ) );
    result.includeHighlighterSet = source.value( "includeHighlighterSet", false );
    return result;
}

} // namespace klogg::server
