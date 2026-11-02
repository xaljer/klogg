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
#include <optional>
#include <string>
#include <vector>

#include "klogg/api_engine.h"
#include "klogg/api_types.h"

// Types of the event visualization API: log -> structured events -> Perfetto.
// Shapes follow the existing implementation (EventVisualizationManager in
// src/state).
namespace klogg::api {

enum class EventType { Instance, Slice };
enum class SliceType { None, Begin, End };
enum class DurationUnit { Ns, Us, Ms, S };

// Log parsing rule: extracts timestamp/pid/tid/module/level from a raw line.
// Embedded in LogTypeConfig, no standalone id.
struct ParsingRule {
    std::string name;
    std::string globPattern;     // file match: "*.log", "/var/log/syslog*"
    std::string parsePattern;    // regex with named groups: timestamp, pid, tid, module, log_level
    std::string timestampFormat; // strptime format or "epoch_s"/"epoch_ms"/"epoch_us"/"epoch_ns"
};

// Event rule: extracts an event from (parsed) lines.
// Embedded in LogTypeConfig, no standalone id.
struct EventRule {
    std::string name;
    std::string category; // grouping: "database", "network", "request"
    std::string description;

    EventType eventType = EventType::Instance;

    // Instance: single pattern match is an event
    std::string pattern;

    // Slice: begin/end pair
    std::string beginPattern;
    std::string endPattern;
    std::string asyncIdParam; // capture group name, empty = per-thread stack matching

    // Complete slice (duration from a single line)
    std::string durationParam; // capture group name
    DurationUnit durationUnit = DurationUnit::Ms;

    std::vector<std::string> parameterNames;    // captured in begin/instance pattern
    std::vector<std::string> endParameterNames; // extra captured in end pattern

    bool enabled = true;
};

// Log type config: parsing rule + event rules grouped by category.
// Replacement happens at this granularity; `name` is the stable identifier the
// extraction call uses (there is no core-assigned opaque id).
struct LogTypeConfig {
    std::string name; // "Java Application Log"
    std::string globPattern;
    ParsingRule parsingRule;
    std::map<std::string, std::vector<EventRule>> eventRulesByCategory;
    bool enabled = true;
    bool isDefault = false;
};

// ---- Extraction ----

using ExtractionId = uint64_t;

struct ExtractionParams {
    // Stable identifier of the config to extract with: its name. Core looks it
    // up in the in-memory list; the first name match wins (names may repeat).
    std::string logTypeConfigName;
    std::optional<LineNumber> fromLine; // nullopt = file start
    std::optional<LineNumber> toLine;   // nullopt = file end
};

struct ExtractionProgress {
    ExtractionId id;
    LinesCount processedLines;
    uint32_t eventsFound;
    float progress; // 0.0 - 1.0
};

struct ExtractedEvent {
    std::string name; // event name (from EventRule)
    std::string category;
    std::string description;

    LineNumber line;
    int64_t timestampNs = 0; // normalized to nanoseconds
    bool hasValidTimestamp = false;
    int64_t pid = 0;
    int64_t tid = 0;
    std::string module;
    std::string logLevel;
    std::map<std::string, std::string> parameters;

    EventType eventType = EventType::Instance;
    SliceType sliceType = SliceType::None;

    // Slice events only:
    int64_t durationNs = 0; // complete slices
    int64_t startTimestampNs = 0;
    std::string traceId;

    std::string rawLine;
};

// Begin/end line range of a paired slice (for the UI slice overlay)
struct SliceRange {
    LineNumber beginLine;
    LineNumber endLine;
    std::string name;
    std::string category;
    std::string traceId;
};

// ---- Export ----

enum class ExportFormat {
    Json,        // Chrome Trace Event JSON
    BinaryProto, // reserved, returns InternalError until implemented
};

struct EventExportResult {
    std::string outputPath;
    int eventCount = 0;
    int skippedCount = 0; // events without a valid timestamp
    int completeEventCount = 0;
    int orphanBeginCount = 0;
    int orphanEndCount = 0;
    bool hasTraceProcessor = false; // trace_processor_shell found on the system
};

} // namespace klogg::api
