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

#include "klogg/api_events.h"

#include "eventvisualization.h"

// The API event types and the native engine types now use the same
// std::string/std::map shapes, so conversions are identity copies. They are
// kept as explicit functions to keep the boundary visible; native timestamps
// and durations are already nanoseconds and pass through unchanged.
namespace klogg::api {

inline ::LogParsingRule nativeParsingRuleFromApi( const ParsingRule& rule )
{
    ::LogParsingRule native;
    native.name = rule.name;
    native.fileGlobPattern = rule.globPattern;
    native.parsePattern = rule.parsePattern;
    native.timestampFormat = rule.timestampFormat;
    return native;
}

inline ParsingRule apiParsingRuleFromNative( const ::LogParsingRule& rule )
{
    ParsingRule api;
    api.name = rule.name;
    api.globPattern = rule.fileGlobPattern;
    api.parsePattern = rule.parsePattern;
    api.timestampFormat = rule.timestampFormat;
    return api;
}

inline ::EventRule nativeEventRuleFromApi( const EventRule& rule )
{
    ::EventRule native;
    native.name = rule.name;
    native.category = rule.category;
    native.description = rule.description;
    native.enabled = rule.enabled;
    native.eventType
        = rule.eventType == EventType::Slice ? ::EventType::Slice : ::EventType::Instance;
    native.pattern = rule.pattern;
    native.beginPattern = rule.beginPattern;
    native.endPattern = rule.endPattern;
    native.asyncIdParam = rule.asyncIdParam;
    native.durationParam = rule.durationParam;
    native.durationUnit = static_cast<::DurationUnit>( static_cast<int>( rule.durationUnit ) );
    native.parameterNames = rule.parameterNames;
    native.endParameterNames = rule.endParameterNames;
    return native;
}

inline EventRule apiEventRuleFromNative( const ::EventRule& rule )
{
    EventRule api;
    api.name = rule.name;
    api.category = rule.category;
    api.description = rule.description;
    api.enabled = rule.enabled;
    api.eventType = rule.eventType == ::EventType::Slice ? EventType::Slice : EventType::Instance;
    api.pattern = rule.pattern;
    api.beginPattern = rule.beginPattern;
    api.endPattern = rule.endPattern;
    api.asyncIdParam = rule.asyncIdParam;
    api.durationParam = rule.durationParam;
    api.durationUnit = static_cast<DurationUnit>( static_cast<int>( rule.durationUnit ) );
    api.parameterNames = rule.parameterNames;
    api.endParameterNames = rule.endParameterNames;
    return api;
}

inline ::LogTypeConfig nativeLogTypeFromApi( const LogTypeConfig& config )
{
    ::LogTypeConfig native;
    native.name = config.name;
    native.globPattern = config.globPattern;
    native.parsingRule = nativeParsingRuleFromApi( config.parsingRule );
    native.enabled = config.enabled;
    native.isDefault = config.isDefault;
    for ( const auto& [ category, rules ] : config.eventRulesByCategory ) {
        auto& nativeRules = native.eventRulesByCategory[ category ];
        nativeRules.reserve( rules.size() );
        for ( const auto& rule : rules ) {
            nativeRules.push_back( nativeEventRuleFromApi( rule ) );
        }
    }
    return native;
}

inline LogTypeConfig apiLogTypeFromNative( const ::LogTypeConfig& config )
{
    LogTypeConfig api;
    api.name = config.name;
    api.globPattern = config.globPattern;
    api.parsingRule = apiParsingRuleFromNative( config.parsingRule );
    api.enabled = config.enabled;
    api.isDefault = config.isDefault;
    for ( const auto& [ category, rules ] : config.eventRulesByCategory ) {
        auto& apiRules = api.eventRulesByCategory[ category ];
        apiRules.reserve( rules.size() );
        for ( const auto& rule : rules ) {
            apiRules.push_back( apiEventRuleFromNative( rule ) );
        }
    }
    return api;
}

inline ExtractedEvent apiEventFromNative( const ::LogEvent& event )
{
    ExtractedEvent api;
    api.name = event.name;
    api.category = event.category;
    api.description = event.description;
    api.line = event.line;
    api.timestampNs = event.timestamp;
    api.hasValidTimestamp = event.hasValidTimestamp;
    api.pid = event.pid;
    api.tid = event.tid;
    api.module = event.module;
    api.logLevel = event.logLevel;
    api.parameters = event.parameters;
    api.eventType = event.eventType == ::EventType::Slice ? EventType::Slice : EventType::Instance;
    switch ( event.sliceType ) {
    case ::SliceType::Begin:
        api.sliceType = SliceType::Begin;
        break;
    case ::SliceType::End:
        api.sliceType = SliceType::End;
        break;
    default:
        api.sliceType = SliceType::None;
        break;
    }
    api.durationNs = event.duration;
    api.startTimestampNs = event.startTimestamp;
    api.traceId = event.traceId;
    api.rawLine = event.rawLine;
    return api;
}

inline SliceRange apiSliceRangeFromNative( const ::SliceLineRange& range )
{
    SliceRange api;
    api.beginLine = range.beginLine;
    api.endLine = range.endLine;
    api.name = range.ruleName;
    api.category = range.category;
    api.traceId = range.traceId;
    return api;
}

} // namespace klogg::api
