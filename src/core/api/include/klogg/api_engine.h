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
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "klogg/api_types.h"

// Types of the engine API: file I/O, line data and search — the core surface.
namespace klogg::api {

struct EncodingInfo {
    std::string name; // "UTF-8", "GBK", "Shift_JIS", ...
    float confidence; // uchardet confidence 0.0-1.0
};

struct AnsiSegment {
    uint32_t start; // byte offset within the line
    uint32_t end;
    std::string fg; // "#ff0000" or ""
    std::string bg;
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

struct LineData {
    LineNumber num;
    std::string text;                  // raw bytes
    std::vector<AnsiSegment> segments; // empty = no ANSI
};

struct FileInfo {
    FileId id;
    std::string path;
    int64_t size_bytes;
    LinesCount total_lines;
    LineLength max_line_length;
    EncodingInfo encoding;
    // Milliseconds since epoch (local time), 0 when the file is missing
    int64_t last_modified_ms;
    bool indexed;
};

// Semantics match AnsiProcessing in configuration.h (None / StripOnly / RenderColors)
enum class AnsiMode {
    Raw,      // bytes as-is (escape sequences kept), segments not filled
    Strip,    // escape sequences removed
    Segments, // escape sequences removed, segments filled
};

// Bit values of the per-line flag bytes returned by Session::lineFlags
enum LineFlag : uint8_t {
    LineFlagMark = 1 << 0,
    LineFlagMatch = 1 << 1,
    // Set when the line holds an event of the file's latest completed
    // extraction
    LineFlagEvent = 1 << 2,
};

struct SearchParams {
    std::string pattern;
    bool caseSensitive = false;
    bool isRegex = true;
    bool invertMatch = false; // select lines that do NOT match the pattern
    // ExprTk boolean combining ("error" and ("timeout" or "panic")); same as
    // runBooleanSearch but honors the other flags (case, invert, plain text)
    bool isBoolean = false;
    std::optional<LineNumber> fromLine; // scan range start, nullopt = file start
    std::optional<LineNumber> toLine;   // scan range end, nullopt = file end
    std::optional<uint32_t> maxResults; // stop after N matches, nullopt = unlimited
};

// Pattern validation result (no search executed); UI uses it for live error feedback
struct PatternInfo {
    bool valid;
    std::string error;
    bool isPlainText;
};

struct SearchResult {
    SearchId id;
    LinesCount total_matches;
    LinesCount processed_lines;
    float progress; // 0.0 - 1.0
};

using FileCallback = std::function<void( Result<FileInfo> )>;
using SearchCallback = std::function<void( Result<SearchResult> )>;
using ProgressCallback = std::function<void( SearchId, LinesCount matched, float progress )>;

} // namespace klogg::api
