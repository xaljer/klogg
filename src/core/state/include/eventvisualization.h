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

#ifndef EVENTVISUALIZATION_H
#define EVENTVISUALIZATION_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "linetypes.h"

class LogData;

enum class EventType {
    Instance,
    Slice
};

enum class SliceType {
    None,
    Begin,
    End
};

enum class DurationUnit {
    Ns,
    Us,
    Ms,
    S
};

struct LogEvent {
    std::string name;
    std::string pattern;
    std::vector<std::string> parameterNames;
    std::string category;
    std::string description;
    LineNumber line;
    int64_t timestamp;
    int64_t pid;
    int64_t tid;
    std::string module;
    std::string logLevel;
    std::map<std::string, std::string> parameters;
    std::string rawLine;

    EventType eventType = EventType::Instance;
    SliceType sliceType = SliceType::None;
    int64_t duration = 0;
    int64_t startTimestamp = 0;
    std::string traceId;

    bool hasValidTimestamp = false;

    LogEvent() : line(0), timestamp(0), pid(0), tid(0) {}
};

struct EventRule {
    std::string name;
    std::string category;
    std::string description;
    bool enabled = true;

    EventType eventType = EventType::Instance;

    // For Instance type: single pattern
    std::string pattern;

    // For Slice type: begin and end patterns
    std::string beginPattern;
    std::string endPattern;
    std::string asyncIdParam; // capture group name for async_id (empty = stack matching)

    // For complete slice (X type with duration)
    std::string durationParam;
    DurationUnit durationUnit = DurationUnit::Ms;

    // Parameters to capture
    std::vector<std::string> parameterNames;
    std::vector<std::string> endParameterNames; // additional params from end pattern

    EventRule() = default;
};

struct LogParsingRule {
    std::string name;
    std::string fileGlobPattern; // Glob pattern for file matching
    std::string parsePattern;
    std::string timestampFormat;
    bool isDefault = false;
    bool enabled = true;

    LogParsingRule() = default;
};

// Log type configuration: groups parsing rule and event rules for a specific log type
struct LogTypeConfig {
    std::string name;        // Display name for this log type
    std::string globPattern; // Glob pattern to match files (e.g., "*.log", "/var/log/syslog*")
    LogParsingRule parsingRule;
    std::map<std::string, std::vector<EventRule>>
        eventRulesByCategory; // category -> event rules

    bool enabled = true;
    bool isDefault = false;

    LogTypeConfig() = default;

    // Get all event rules across all categories
    std::vector<EventRule> getAllEventRules() const
    {
        std::vector<EventRule> all;
        for ( const auto& [ category, rules ] : eventRulesByCategory ) {
            for ( const auto& rule : rules ) {
                all.push_back( rule );
            }
        }
        return all;
    }

    // Get categories
    std::vector<std::string> getCategories() const
    {
        std::vector<std::string> categories;
        for ( const auto& [ category, rules ] : eventRulesByCategory ) {
            categories.push_back( category );
        }
        return categories;
    }
};

struct PerfettoEvent {
    std::string name;
    int64_t ts = 0;
    int64_t pid = 0;
    int64_t tid = 0;
    std::string ph;
    std::string cat;
    std::map<std::string, std::string> args;

    int64_t dur = 0;
    std::string id;
};

struct SliceLineRange {
    std::string ruleName;
    std::string category;
    std::string description;

    LineNumber beginLine;
    LineNumber endLine;

    int64_t startTimestamp = 0;
    int64_t endTimestamp = 0;
    int64_t duration = 0;

    int64_t pid = 0;
    int64_t tid = 0;
    std::string traceId;

    bool openEnded = false;
};

enum class ExportFormat {
    Json,
    BinaryProto
};

class EventExporter {
  public:
    virtual ~EventExporter() = default;

    virtual bool exportToFile( const std::string& filename,
                               const std::vector<PerfettoEvent>& events ) const = 0;

    virtual std::string name() const = 0;
    virtual std::string fileFilter() const = 0;
    virtual std::string defaultExtension() const = 0;
};

class JsonExporter : public EventExporter {
  public:
    bool exportToFile( const std::string& filename,
                       const std::vector<PerfettoEvent>& events ) const override;

    std::string name() const override { return "JSON"; }

    std::string fileFilter() const override { return "JSON Files (*.json)"; }

    std::string defaultExtension() const override { return ".json"; }
};

class BinaryProtoExporter : public EventExporter {
  public:
    bool exportToFile( const std::string& filename,
                       const std::vector<PerfettoEvent>& events ) const override;

    std::string name() const override { return "Perfetto Binary"; }

    std::string fileFilter() const override { return "Perfetto Binary (*.perfetto)"; }

    std::string defaultExtension() const override { return ".perfetto"; }
};

// Configuration load result
struct ConfigurationLoadResult {
    bool success = false;
    std::string errorMessage;
    int logTypeCount = 0;
    int eventRuleCount = 0;
    std::vector<std::string> warnings;
};

class EventVisualizationManager {
  public:
    static EventVisualizationManager& getInstance();

    // Public so non-singleton owners (e.g. the API session) can hold their
    // own instance; getInstance() remains for the UI side
    EventVisualizationManager() = default;

    static std::string generateRegexFromText( const std::string& selectedText );

    LogEvent parseLine( std::string_view line, const EventRule& rule, LineNumber lineNumber,
                        const LogParsingRule* parsingRule = nullptr );

    // Log type management
    void addLogType( const LogTypeConfig& logType );
    void updateLogType( const std::string& name, const LogTypeConfig& logType );
    void removeLogType( const std::string& name );
    const std::vector<LogTypeConfig>& getLogTypes() const;
    void setLogTypes( const std::vector<LogTypeConfig>& logTypes );
    LogTypeConfig* findLogTypeForFile( const std::string& filePath );
    const LogTypeConfig* findLogTypeForFile( const std::string& filePath ) const;
    bool hasAnyLogType() const;
    const LogTypeConfig* getDefaultLogType() const;

    // Convenience methods for backward compatibility
    void addEventRule( const EventRule& rule );
    void updateEventRule( const std::string& name, const EventRule& rule );
    void removeEventRule( const std::string& name );
    std::vector<EventRule> getEventRules() const;
    void setEventRules( const std::vector<EventRule>& rules );
    std::vector<EventRule> findMatchingRules( const std::string& line ) const;

    // Legacy methods for parsing rules (delegated to log types)
    void addLogParsingRule( const LogParsingRule& rule );
    std::vector<LogParsingRule> getLogParsingRules() const;
    void setLogParsingRules( const std::vector<LogParsingRule>& rules );
    LogParsingRule* findParsingRuleForFile( const std::string& filePath );
    const LogParsingRule* findParsingRuleForFile( const std::string& filePath ) const;
    bool hasAnyParsingRule() const;
    const LogParsingRule* getDefaultParsingRule() const;

    std::vector<PerfettoEvent>
    convertToPerfettoFormat( const std::vector<LogEvent>& events ) const;

    static std::unique_ptr<EventExporter> createExporter( ExportFormat format );
    static std::vector<std::pair<ExportFormat, std::string>> getSupportedFormats();

    // Pure value <-> string (de)serialization of logTypes_ in the historical
    // log_visualization.json format; the file read/write lives in the shell.
    std::string serializeConfiguration() const;
    ConfigurationLoadResult deserializeConfiguration( const std::string& json );

    std::vector<LogEvent> extractEventsFromFile( const std::string& filename,
                                                 const std::vector<EventRule>& rules );

    // Extract events from existing search results in LogFilteredData
    // This is the preferred method - uses the same efficient search as klogg's main search
    std::vector<LogEvent>
    extractEventsFromSearchResults( const LogData* logData,
                                    const std::vector<LineNumber>& matchingLines,
                                    const std::vector<EventRule>& rules );

    // Same as above, but with an explicit parsing rule instead of the first
    // enabled one from the configured log types
    std::vector<LogEvent>
    extractEventsFromSearchResults( const LogData* logData,
                                    const std::vector<LineNumber>& matchingLines,
                                    const std::vector<EventRule>& rules,
                                    const LogParsingRule* parsingRule );

    std::vector<SliceLineRange> buildSliceRanges( const std::vector<LogEvent>& events,
                                                  LineNumber lastLine ) const;

    // Perform complete Perfetto export (extract, convert, write)
    // Thread-safe, can be called from background thread
    struct ExportResult {
        bool success = false;
        std::string outputFile;
        int eventCount = 0;
        int skippedCount = 0;
        int completeEventCount = 0;
        int orphanBeginCount = 0;
        int orphanEndCount = 0;
        std::string errorMessage;
        bool hasTraceProcessor = false;
    };
    ExportResult exportToPerfetto( const std::string& sourceFileName,
                                   const std::vector<EventRule>& rules, const LogData* logData,
                                   const std::vector<LineNumber>& matchingLines );

  private:
    int64_t parseTimestamp( std::string_view timestampStr,
                            std::string_view timestampFormat = {} ) const;

    std::vector<LogTypeConfig> logTypes_;
};

#endif
