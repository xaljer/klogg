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

#include "eventvisualization.h"

#include "log.h"
#include "logdata.h"

#include "pcre2regex.h"

using klogg::Pcre2Regex;
using klogg::Pcre2RegexMatch;
using klogg::Pcre2RegexMatchIterator;

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

EventVisualizationManager& EventVisualizationManager::getInstance()
{
    static EventVisualizationManager instance;
    return instance;
}

namespace {

std::string eventTypeToString( EventType type )
{
    switch ( type ) {
    case EventType::Instance:
        return "instance";
    case EventType::Slice:
        return "slice";
    }
    return "instance";
}

std::string durationUnitToString( DurationUnit unit )
{
    switch ( unit ) {
    case DurationUnit::Ns:
        return "ns";
    case DurationUnit::Us:
        return "us";
    case DurationUnit::Ms:
        return "ms";
    case DurationUnit::S:
        return "s";
    }
    return "ms";
}

EventType parseEventType( const nlohmann::json& value )
{
    std::string str = value.is_string() ? value.get<std::string>() : "";
    std::transform( str.begin(), str.end(), str.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    if ( str == "slice" ) {
        return EventType::Slice;
    }
    return EventType::Instance;
}

DurationUnit parseDurationUnit( const nlohmann::json& value )
{
    std::string str = value.is_string() ? value.get<std::string>() : "";
    std::transform( str.begin(), str.end(), str.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    if ( str == "ns" ) {
        return DurationUnit::Ns;
    }
    if ( str == "us" ) {
        return DurationUnit::Us;
    }
    if ( str == "s" ) {
        return DurationUnit::S;
    }
    return DurationUnit::Ms;
}

std::string getWordBefore( std::string_view text, int startPos )
{
    if ( startPos <= 0 ) {
        return "param";
    }

    int wordStart = startPos - 1;
    while ( wordStart >= 0 && ( text[ static_cast<size_t>( wordStart ) ] == ' ' || text[ static_cast<size_t>( wordStart ) ] == '\t'
                                || text[ static_cast<size_t>( wordStart ) ] == '\n' || text[ static_cast<size_t>( wordStart ) ] == '\r' ) ) {
        --wordStart;
    }

    if ( wordStart < 0 ) {
        return "param";
    }

    int wordEnd = wordStart;
    while ( wordEnd >= 0 && ( std::isalnum( static_cast<unsigned char>( text[ static_cast<size_t>( wordEnd ) ] ) )
                              || text[ static_cast<size_t>( wordEnd ) ] == '_' ) ) {
        --wordEnd;
    }

    std::string word( text.substr( static_cast<size_t>( wordEnd + 1 ),
                                   static_cast<size_t>( wordStart - wordEnd ) ) );
    if ( word.empty() ) {
        return "param";
    }

    word.erase( std::remove_if( word.begin(), word.end(), []( unsigned char c ) {
                    return !std::isalnum( c ) && c != '_';
                } ),
                word.end() );
    if ( word.empty() ) {
        return "param";
    }

    return word;
}

int currentYear()
{
    std::time_t now = std::time( nullptr );
    std::tm tm{};
#ifdef _WIN32
    localtime_s( &tm, &now );
#else
    localtime_r( &now, &tm );
#endif
    return tm.tm_year + 1900;
}

bool isLeapYear( int year )
{
    return ( year % 4 == 0 && year % 100 != 0 ) || ( year % 400 == 0 );
}

int daysInMonth( int year, int month )
{
    static const int days[ 12 ] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if ( month == 2 && isLeapYear( year ) ) {
        return 29;
    }
    return days[ month ];
}

// Maps QDateTime format tokens to strftime tokens. Longest names first so
// "yyyy" is replaced before "yy" and "MMM" before "MM".
std::string toStrftimeFormat( std::string_view format )
{
    static const std::vector<std::pair<std::string, std::string>> replacements = {
        { "yyyy", "%Y" }, { "yy", "%y" }, { "MMM", "%b" }, { "MM", "%m" },
        { "dd", "%d" },   { "HH", "%H" }, { "mm", "%M" },  { "ss", "%S" },
    };

    std::string result( format );
    for ( const auto& [ from, to ] : replacements ) {
        std::string search = from;
        size_t pos = 0;
        while ( ( pos = result.find( search, pos ) ) != std::string::npos ) {
            result.replace( pos, search.size(), to );
            pos += to.size();
        }
    }
    return result;
}

// Parses timestamps whose format contains a zzz (milliseconds) part. The
// strftime family has no millisecond token, so the zzz part is split off and
// handled explicitly.
std::optional<int64_t> parseTimestampWithFormat( std::string_view str, std::string_view format )
{
    std::string formatStr( format );
    size_t zzzPos = formatStr.find( "zzz" );

    std::string prefixFormat = zzzPos == std::string::npos ? formatStr : formatStr.substr( 0, zzzPos );
    std::string suffix = zzzPos == std::string::npos ? "" : formatStr.substr( zzzPos + 3 );

    int millis = 0;
    std::string parseStr( str );
    if ( zzzPos != std::string::npos ) {
        if ( str.size() < zzzPos + 3 + suffix.size() ) {
            return std::nullopt;
        }
        std::string msPart( str.substr( zzzPos, 3 ) );
        if ( msPart.size() != 3 || !std::all_of( msPart.begin(), msPart.end(),
                                                 []( unsigned char c ) { return std::isdigit( c ); } ) ) {
            return std::nullopt;
        }
        millis = ( msPart[ 0 ] - '0' ) * 100 + ( msPart[ 1 ] - '0' ) * 10 + ( msPart[ 2 ] - '0' );

        // The remainder of the string must match the format tail literally.
        if ( str.size() != zzzPos + 3 + suffix.size() || str.substr( zzzPos + 3 ) != suffix ) {
            return std::nullopt;
        }
        parseStr = std::string( str.substr( 0, zzzPos ) );
    }

    std::tm tm{};
    tm.tm_isdst = -1;
    tm.tm_mday = 1;
    tm.tm_mon = 0;

    // Qt's addYears fallback: formats without a year field are interpreted
    // relative to the current year.
    const bool hasYear = formatStr.find( "yyyy" ) != std::string::npos || formatStr.find( "yy" ) != std::string::npos;
    tm.tm_year = hasYear ? 0 : currentYear() - 1900;

    std::istringstream stream( parseStr );
    stream.imbue( std::locale::classic() );
    stream >> std::get_time( &tm, toStrftimeFormat( prefixFormat ).c_str() );
    if ( stream.fail() ) {
        return std::nullopt;
    }

    // Qt requires the whole string to match the format.
    if ( stream.peek() != std::char_traits<char>::eof() ) {
        return std::nullopt;
    }

    if ( tm.tm_mon < 0 || tm.tm_mon > 11 || tm.tm_mday < 1 || tm.tm_mday > daysInMonth( tm.tm_year + 1900, tm.tm_mon )
         || tm.tm_hour < 0 || tm.tm_hour > 23 || tm.tm_min < 0 || tm.tm_min > 59 || tm.tm_sec < 0
         || tm.tm_sec > 59 ) {
        return std::nullopt;
    }

    std::time_t epoch = std::mktime( &tm );
    if ( epoch == static_cast<std::time_t>( -1 ) ) {
        return std::nullopt;
    }

    return static_cast<int64_t>( epoch ) * 1000000000LL + static_cast<int64_t>( millis ) * 1000000LL;
}

// Registry of compiled parse patterns. Compiled lazily; an invalid pattern
// produces an empty (never-matching) regex. Concurrent first-use from two
// threads may race on compile; the old QRegularExpression-based cache shared
// the same lazy-compilation race.
std::map<std::string, klogg::Pcre2Regex>& parsingRegexCache()
{
    static std::map<std::string, klogg::Pcre2Regex> cache;
    return cache;
}

std::string applicationDirPath()
{
#ifdef _WIN32
    std::vector<wchar_t> buffer( MAX_PATH );
    DWORD length = GetModuleFileNameW( nullptr, buffer.data(), static_cast<DWORD>( buffer.size() ) );
    while ( length == buffer.size() && length != 0 ) {
        buffer.resize( buffer.size() * 2 );
        length = GetModuleFileNameW( nullptr, buffer.data(), static_cast<DWORD>( buffer.size() ) );
    }
    if ( length == 0 ) {
        return "";
    }
    int wideLength = static_cast<int>( length );
    int size = WideCharToMultiByte( CP_UTF8, 0, buffer.data(), wideLength, nullptr, 0, nullptr, nullptr );
    std::string result( static_cast<size_t>( size ), '\0' );
    WideCharToMultiByte( CP_UTF8, 0, buffer.data(), wideLength, result.data(), size, nullptr, nullptr );
    size_t lastSeparator = result.find_last_of( "\\/" );
    return lastSeparator == std::string::npos ? "" : result.substr( 0, lastSeparator );
#elif defined(__linux__)
    std::vector<char> buffer( 4096 );
    ssize_t length = readlink( "/proc/self/exe", buffer.data(), buffer.size() - 1 );
    if ( length <= 0 ) {
        return "";
    }
    buffer[ static_cast<size_t>( length ) ] = '\0';
    std::string result( buffer.data(), static_cast<size_t>( length ) );
    size_t lastSeparator = result.find_last_of( '/' );
    return lastSeparator == std::string::npos ? "" : result.substr( 0, lastSeparator );
#else
    return "";
#endif
}

// Qt's QJsonValue::toString()/toBool()/toArray()/toObject() semantics: wrong
// types or missing keys yield the default/empty value instead of an error.
const nlohmann::json* jsonMember( const nlohmann::json& obj, const char* key )
{
    if ( !obj.is_object() ) {
        return nullptr;
    }
    auto it = obj.find( key );
    if ( it == obj.end() || it->is_null() ) {
        return nullptr;
    }
    return &*it;
}

std::string jsonString( const nlohmann::json& obj, const char* key )
{
    const nlohmann::json* value = jsonMember( obj, key );
    return value && value->is_string() ? value->get<std::string>() : "";
}

bool jsonBool( const nlohmann::json& obj, const char* key, bool defaultValue )
{
    const nlohmann::json* value = jsonMember( obj, key );
    return value && value->is_boolean() ? value->get<bool>() : defaultValue;
}

nlohmann::json jsonArray( const nlohmann::json& obj, const char* key )
{
    const nlohmann::json* value = jsonMember( obj, key );
    return value && value->is_array() ? *value : nlohmann::json::array();
}

nlohmann::json jsonObject( const nlohmann::json& obj, const char* key )
{
    const nlohmann::json* value = jsonMember( obj, key );
    return value && value->is_object() ? *value : nlohmann::json::object();
}

bool parseInteger( std::string_view str, int64_t& result )
{
    const char* begin = str.data();
    const char* end = str.data() + str.size();
    auto parsed = std::from_chars( begin, end, result );
    return parsed.ec == std::errc() && parsed.ptr == end;
}

} // namespace

std::string EventVisualizationManager::generateRegexFromText( const std::string& selectedText )
{
    if ( selectedText.empty() ) {
        return "";
    }

    std::string result = selectedText;

    struct MatchInfo {
        int start;
        int length;
        std::string replacement;
    };
    std::vector<MatchInfo> replacements;

    std::vector<std::pair<std::string, std::string>> patterns = {
        { R"(\{([^{}]+)\})", "brace" },
        { R"(\<([^<>]+)\>)", "angle" },
        { R"(\[([^\[\]]+)\])", "bracket" },
        { R"(\(([^()]+)\))", "paren" },
        { R"q("([^"]+)")q", "quote" },
        { R"(\s+:\s*(\S+))", "colon" },
    };

    for ( const auto& [ pattern, type ] : patterns ) {
        klogg::Pcre2Regex regex( pattern );
        klogg::Pcre2RegexMatchIterator it = regex.globalMatch( result );
        while ( it.hasNext() ) {
            klogg::Pcre2RegexMatch match = it.next();
            auto captured = match.captured( 1 );
            if ( !captured ) {
                continue;
            }

            int matchStart = static_cast<int>( match.capturedStart() );
            std::string prefixWord = getWordBefore( result, matchStart );
            // PCRE named groups must not start with a digit; timestamp
            // fragments right before a bracket are a common source of those
            if ( !prefixWord.empty() && std::isdigit( static_cast<unsigned char>( prefixWord.front() ) ) ) {
                prefixWord = "p" + prefixWord;
            }

            std::string captureGroup;
            if ( type == "colon" ) {
                captureGroup = "(?<" + prefixWord + ">" + klogg::Pcre2Regex::escape( *captured ) + ")";
            } else if ( type == "quote" ) {
                captureGroup = "(?<" + prefixWord + ">[^\"]*)";
            } else {
                captureGroup = "(?<" + prefixWord + ">[^\\s]+)";
            }

            replacements.push_back( { matchStart, static_cast<int>( match.capturedLength() ), captureGroup } );
        }
    }

    std::sort( replacements.begin(), replacements.end(), []( const MatchInfo& a, const MatchInfo& b ) {
        return a.start > b.start;
    } );

    std::set<std::string> usedNames;
    klogg::Pcre2Regex nameRegex( R"(\?<([^>]+)>)" );
    for ( auto& rep : replacements ) {
        klogg::Pcre2RegexMatch nameMatch = nameRegex.match( rep.replacement );
        if ( nameMatch.hasMatch() ) {
            std::string baseName( nameMatch.captured( 1 ).value_or( "" ) );
            std::string finalName = baseName;
            int counter = 1;
            while ( usedNames.count( finalName ) ) {
                finalName = baseName + "_" + std::to_string( counter++ );
            }
            usedNames.insert( finalName );
            rep.replacement.replace( static_cast<size_t>( nameMatch.capturedStart() ),
                                     static_cast<size_t>( nameMatch.capturedLength() ), "?<" + finalName + ">" );
        }
    }

    for ( const auto& rep : replacements ) {
        result.replace( static_cast<size_t>( rep.start ), static_cast<size_t>( rep.length ), rep.replacement );
    }

    return result;
}

int64_t EventVisualizationManager::parseTimestamp( std::string_view timestampStr,
                                                   std::string_view timestampFormat ) const
{
    if ( timestampStr.empty() ) {
        return 0;
    }

    if ( !timestampFormat.empty() ) {
        if ( timestampFormat == "unix_s" || timestampFormat == "unix_ms" ) {
            int64_t ts = 0;
            if ( parseInteger( timestampStr, ts ) ) {
                return timestampFormat == "unix_s" ? ts * 1000000000LL : ts * 1000000LL;
            }
            return 0;
        }

        if ( auto parsed = parseTimestampWithFormat( timestampStr, timestampFormat ) ) {
            return *parsed;
        }
    }

    const std::vector<std::string> formats = {
        "yyyy-MM-ddTHH:mm:ss.zzz", "yyyy-MM-ddTHH:mm:ss",  "yyyy-MM-dd HH:mm:ss.zzz",
        "yyyy-MM-dd HH:mm:ss",     "yyyy/MM/dd HH:mm:ss.zzz", "yyyy/MM/dd HH:mm:ss",
        "HH:mm:ss.zzz",            "HH:mm:ss",              "MMM dd HH:mm:ss",
        "dd/MMM/yyyy:HH:mm:ss",
    };

    for ( const auto& format : formats ) {
        if ( auto parsed = parseTimestampWithFormat( timestampStr, format ) ) {
            return *parsed;
        }
    }

    int64_t ts = 0;
    if ( parseInteger( timestampStr, ts ) ) {
        if ( ts < 1000000000000LL ) {
            ts *= 1000000000LL;
        } else if ( ts < 1000000000000000LL ) {
            ts *= 1000000LL;
        }
        return ts;
    }

    return 0;
}

int64_t convertToNanoseconds( int64_t value, DurationUnit unit )
{
    switch ( unit ) {
    case DurationUnit::Ns:
        return value;
    case DurationUnit::Us:
        return value * 1000LL;
    case DurationUnit::Ms:
        return value * 1000000LL;
    case DurationUnit::S:
        return value * 1000000000LL;
    default:
        return value;
    }
}

LogEvent EventVisualizationManager::parseLine( std::string_view line, const EventRule& rule,
                                               LineNumber lineNumber,
                                               const LogParsingRule* parsingRule )
{
    LogEvent event;
    event.name = rule.name;
    event.category = rule.category;
    event.description = rule.description;
    event.line = lineNumber;
    event.rawLine = line;
    event.eventType = rule.eventType;

    std::string pattern;
    std::vector<std::string> paramNames;

    if ( rule.eventType == EventType::Instance ) {
        pattern = rule.pattern;
        paramNames = rule.parameterNames;
        event.sliceType = SliceType::None;
    } else {
        // For Slice type, try begin pattern first, then end pattern
        if ( !rule.beginPattern.empty() ) {
            klogg::Pcre2Regex beginRegex( rule.beginPattern, klogg::Pcre2Regex::CaseInsensitiveOption );
            klogg::Pcre2RegexMatch beginMatch = beginRegex.match( line );

            if ( beginMatch.hasMatch() ) {
                pattern = rule.beginPattern;
                paramNames = rule.parameterNames;
                // A durationParam turns a begin-pattern match into a complete slice
                event.sliceType = rule.durationParam.empty() ? SliceType::Begin : SliceType::None;
                event.pattern = rule.beginPattern;
            } else if ( !rule.endPattern.empty() ) {
                klogg::Pcre2Regex endRegex( rule.endPattern, klogg::Pcre2Regex::CaseInsensitiveOption );
                klogg::Pcre2RegexMatch endMatch = endRegex.match( line );

                if ( endMatch.hasMatch() ) {
                    pattern = rule.endPattern;
                    paramNames = rule.endParameterNames;
                    event.sliceType = SliceType::End;
                    event.pattern = rule.endPattern;
                }
            }
        }

        // Safety fallback: if endParameterNames are not configured, use parameterNames for End pattern.
        if ( event.sliceType == SliceType::End && paramNames.empty() ) {
            paramNames = rule.parameterNames;
        }
    }

    if ( !pattern.empty() ) {
        klogg::Pcre2Regex regex( pattern, klogg::Pcre2Regex::CaseInsensitiveOption );
        klogg::Pcre2RegexMatch match = regex.match( line );

        if ( match.hasMatch() ) {
            event.pattern = pattern;

            for ( const auto& paramName : paramNames ) {
                // Qt never leaves gaps: a group that does not capture yields ""
                event.parameters[ paramName ] = std::string( match.captured( paramName ).value_or( "" ) );
            }

            // Extract async_id for Slice events
            if ( rule.eventType == EventType::Slice && !rule.asyncIdParam.empty() ) {
                event.traceId = std::string( match.captured( rule.asyncIdParam ).value_or( "" ) );
            }

            // Handle duration for complete slice
            if ( rule.eventType == EventType::Slice && event.sliceType == SliceType::None
                 && !rule.durationParam.empty() ) {
                std::string durationStr = std::string( match.captured( rule.durationParam ).value_or( "" ) );
                if ( !durationStr.empty() ) {
                    int64_t durationValue = 0;
                    if ( parseInteger( durationStr, durationValue ) ) {
                        event.duration = convertToNanoseconds( durationValue, rule.durationUnit );
                    }
                }
            }
        }
    }

    // Parse common fields from parsing rule
    if ( parsingRule && parsingRule->enabled && !parsingRule->parsePattern.empty() ) {
        auto& cache = parsingRegexCache();
        auto cacheIt = cache.find( parsingRule->parsePattern );
        if ( cacheIt == cache.end() ) {
            cacheIt = cache.emplace( parsingRule->parsePattern,
                                     klogg::Pcre2Regex( parsingRule->parsePattern ) ).first;
        }
        const klogg::Pcre2Regex& parseRegex = cacheIt->second;
        klogg::Pcre2RegexMatch parseMatch = parseRegex.match( line );

        if ( parseMatch.hasMatch() ) {
            std::string timestampStr
                = std::string( parseMatch.captured( "timestamp" ).value_or( "" ) );
            if ( !timestampStr.empty() ) {
                // Note: customFormat is read from the parsing rule; the cached
                // regex is keyed by parsePattern only, so the timestamp format
                // cannot be part of the key.
                std::string customFormat = parsingRule->timestampFormat;
                int64_t parsedTs = parseTimestamp( timestampStr, customFormat );
                if ( parsedTs > 0 ) {
                    event.timestamp = parsedTs;
                    event.hasValidTimestamp = true;
                }
            }

            std::string pidStr = std::string( parseMatch.captured( "pid" ).value_or( "" ) );
            if ( !pidStr.empty() ) {
                if ( !parseInteger( pidStr, event.pid ) ) {
                    event.pid = 0;
                }
            }

            std::string tidStr = std::string( parseMatch.captured( "tid" ).value_or( "" ) );
            if ( !tidStr.empty() ) {
                if ( !parseInteger( tidStr, event.tid ) ) {
                    event.tid = 0;
                }
            }

            event.module = std::string( parseMatch.captured( "module" ).value_or( "" ) );
            event.logLevel = std::string( parseMatch.captured( "log_level" ).value_or( "" ) );
        }
    }

    // Calculate start timestamp for complete slice
    if ( event.eventType == EventType::Slice && event.sliceType == SliceType::None
         && event.duration > 0 ) {
        event.startTimestamp = event.timestamp - event.duration;
    } else {
        event.startTimestamp = event.timestamp;
    }

    return event;
}

// Log type management
void EventVisualizationManager::addLogType( const LogTypeConfig& logType )
{
    for ( size_t i = 0; i < logTypes_.size(); ++i ) {
        if ( logTypes_[ i ].name == logType.name ) {
            logTypes_[ i ] = logType;
            return;
        }
    }
    logTypes_.push_back( logType );
}

void EventVisualizationManager::updateLogType( const std::string& name, const LogTypeConfig& logType )
{
    for ( size_t i = 0; i < logTypes_.size(); ++i ) {
        if ( logTypes_[ i ].name == name ) {
            logTypes_[ i ] = logType;
            return;
        }
    }
    logTypes_.push_back( logType );
}

void EventVisualizationManager::removeLogType( const std::string& name )
{
    for ( size_t i = 0; i < logTypes_.size(); ++i ) {
        if ( logTypes_[ i ].name == name ) {
            logTypes_.erase( logTypes_.begin() + static_cast<std::ptrdiff_t>( i ) );
            return;
        }
    }
}

const std::vector<LogTypeConfig>& EventVisualizationManager::getLogTypes() const
{
    return logTypes_;
}

void EventVisualizationManager::setLogTypes( const std::vector<LogTypeConfig>& logTypes )
{
    logTypes_ = logTypes;
}

LogTypeConfig* EventVisualizationManager::findLogTypeForFile( const std::string& filePath )
{
    if ( logTypes_.empty() ) {
        return nullptr;
    }

    std::error_code ec;
    std::string fileName = std::filesystem::path( filePath ).filename().string();
    std::string absolutePath = std::filesystem::absolute( filePath, ec ).string();

    for ( auto& logType : logTypes_ ) {
        if ( !logType.enabled || logType.globPattern.empty() ) {
            continue;
        }

        klogg::Pcre2Regex globRegex = klogg::Pcre2Regex::fromWildcard( logType.globPattern, false );
        if ( globRegex.match( fileName ).hasMatch() || globRegex.match( absolutePath ).hasMatch() ) {
            return &logType;
        }
    }

    return const_cast<LogTypeConfig*>( getDefaultLogType() );
}

const LogTypeConfig* EventVisualizationManager::findLogTypeForFile( const std::string& filePath ) const
{
    if ( logTypes_.empty() ) {
        return nullptr;
    }

    std::error_code ec;
    std::string fileName = std::filesystem::path( filePath ).filename().string();
    std::string absolutePath = std::filesystem::absolute( filePath, ec ).string();

    for ( const auto& logType : logTypes_ ) {
        if ( !logType.enabled || logType.globPattern.empty() ) {
            continue;
        }

        klogg::Pcre2Regex globRegex = klogg::Pcre2Regex::fromWildcard( logType.globPattern, false );
        if ( globRegex.match( fileName ).hasMatch() || globRegex.match( absolutePath ).hasMatch() ) {
            return &logType;
        }
    }

    return getDefaultLogType();
}

bool EventVisualizationManager::hasAnyLogType() const
{
    for ( const auto& logType : logTypes_ ) {
        if ( logType.enabled && !logType.parsingRule.parsePattern.empty() ) {
            return true;
        }
    }
    return false;
}

const LogTypeConfig* EventVisualizationManager::getDefaultLogType() const
{
    for ( const auto& logType : logTypes_ ) {
        if ( logType.enabled && logType.isDefault && !logType.parsingRule.parsePattern.empty() ) {
            return &logType;
        }
    }
    return nullptr;
}

// Convenience methods for backward compatibility
void EventVisualizationManager::addEventRule( const EventRule& rule )
{
    // Add to first log type's first category, or create "General" category
    if ( logTypes_.empty() ) {
        LogTypeConfig defaultType;
        defaultType.name = "Default";
        defaultType.globPattern = "*";
        defaultType.isDefault = true;
        logTypes_.push_back( defaultType );
    }

    LogTypeConfig& logType = logTypes_.front();
    std::string category = rule.category.empty() ? "General" : rule.category;

    // Check if rule already exists
    auto& categoryRules = logType.eventRulesByCategory[ category ];
    for ( size_t i = 0; i < categoryRules.size(); ++i ) {
        if ( categoryRules[ i ].name == rule.name ) {
            categoryRules[ i ] = rule;
            return;
        }
    }
    categoryRules.push_back( rule );
}

void EventVisualizationManager::updateEventRule( const std::string& name, const EventRule& rule )
{
    for ( auto& logType : logTypes_ ) {
        for ( auto& [ category, rules ] : logType.eventRulesByCategory ) {
            for ( size_t i = 0; i < rules.size(); ++i ) {
                if ( rules[ i ].name == name ) {
                    rules[ i ] = rule;
                    return;
                }
            }
        }
    }
    addEventRule( rule );
}

void EventVisualizationManager::removeEventRule( const std::string& name )
{
    for ( auto& logType : logTypes_ ) {
        for ( auto& [ category, rules ] : logType.eventRulesByCategory ) {
            for ( size_t i = 0; i < rules.size(); ++i ) {
                if ( rules[ i ].name == name ) {
                    rules.erase( rules.begin() + static_cast<std::ptrdiff_t>( i ) );
                    return;
                }
            }
        }
    }
}

std::vector<EventRule> EventVisualizationManager::getEventRules() const
{
    std::vector<EventRule> all;
    for ( const auto& logType : logTypes_ ) {
        std::vector<EventRule> logTypeRules = logType.getAllEventRules();
        all.insert( all.end(), logTypeRules.begin(), logTypeRules.end() );
    }
    return all;
}

void EventVisualizationManager::setEventRules( const std::vector<EventRule>& rules )
{
    if ( logTypes_.empty() ) {
        LogTypeConfig defaultType;
        defaultType.name = "Default";
        defaultType.globPattern = "*";
        defaultType.isDefault = true;
        logTypes_.push_back( defaultType );
    }

    logTypes_.front().eventRulesByCategory.clear();
    for ( const auto& rule : rules ) {
        std::string category = rule.category.empty() ? "General" : rule.category;
        logTypes_.front().eventRulesByCategory[ category ].push_back( rule );
    }
}

std::vector<EventRule> EventVisualizationManager::findMatchingRules( const std::string& line ) const
{
    std::vector<EventRule> matches;

    for ( const auto& logType : logTypes_ ) {
        if ( !logType.enabled ) {
            continue;
        }

        for ( const auto& [ category, rules ] : logType.eventRulesByCategory ) {
            for ( const auto& rule : rules ) {
                if ( !rule.enabled ) {
                    continue;
                }

                // Pre-compile regex for this check
                if ( rule.eventType == EventType::Instance ) {
                    if ( rule.pattern.empty() ) {
                        continue;
                    }
                    klogg::Pcre2Regex regex( rule.pattern, klogg::Pcre2Regex::CaseInsensitiveOption );
                    if ( regex.match( line ).hasMatch() ) {
                        matches.push_back( rule );
                    }
                } else {
                    bool ruleMatches = false;
                    if ( !rule.beginPattern.empty() ) {
                        klogg::Pcre2Regex beginRegex(
                            rule.beginPattern, klogg::Pcre2Regex::CaseInsensitiveOption );
                        ruleMatches = beginRegex.match( line ).hasMatch();
                    }
                    if ( !ruleMatches && !rule.endPattern.empty() ) {
                        klogg::Pcre2Regex endRegex( rule.endPattern,
                                                    klogg::Pcre2Regex::CaseInsensitiveOption );
                        ruleMatches = endRegex.match( line ).hasMatch();
                    }

                    if ( ruleMatches ) {
                        matches.push_back( rule );
                    }
                }
            }
        }
    }

    return matches;
}

// Legacy parsing rule methods
void EventVisualizationManager::addLogParsingRule( const LogParsingRule& rule )
{
    // Create or update a log type for this parsing rule
    // This is for backward compatibility
    if ( logTypes_.empty() ) {
        LogTypeConfig defaultType;
        defaultType.name = rule.name.empty() ? "Default" : rule.name;
        defaultType.globPattern = "*";
        defaultType.parsingRule = rule;
        defaultType.isDefault = true;
        logTypes_.push_back( defaultType );
    } else {
        logTypes_.front().parsingRule = rule;
    }
}

std::vector<LogParsingRule> EventVisualizationManager::getLogParsingRules() const
{
    std::vector<LogParsingRule> rules;
    for ( const auto& logType : logTypes_ ) {
        if ( !logType.parsingRule.parsePattern.empty() ) {
            rules.push_back( logType.parsingRule );
        }
    }
    return rules;
}

void EventVisualizationManager::setLogParsingRules( const std::vector<LogParsingRule>& rules )
{
    // Backward compatibility API: update parsing rules without destroying event rules.
    if ( rules.empty() ) {
        return;
    }

    // If there is no existing log type configuration, create it from parsing rules.
    if ( logTypes_.empty() ) {
        for ( const auto& rule : rules ) {
            LogTypeConfig logType;
            logType.name = rule.name.empty() ? "Default" : rule.name;
            logType.globPattern = rule.fileGlobPattern.empty() ? "*" : rule.fileGlobPattern;
            logType.parsingRule = rule;
            logType.isDefault = false;
            logTypes_.push_back( logType );
        }
        if ( !logTypes_.empty() ) {
            logTypes_.front().isDefault = true;
        }
        return;
    }

    // Update existing log types in order to keep backward compatibility and preserve event rules.
    const size_t updates = std::min( logTypes_.size(), rules.size() );
    for ( size_t i = 0; i < updates; ++i ) {
        logTypes_[ i ].parsingRule = rules[ i ];
        if ( !rules[ i ].name.empty() ) {
            logTypes_[ i ].name = rules[ i ].name;
        }
        if ( !rules[ i ].fileGlobPattern.empty() ) {
            logTypes_[ i ].globPattern = rules[ i ].fileGlobPattern;
        }
    }

    // If there are more parsing rules than current log types, append new ones.
    for ( size_t i = updates; i < rules.size(); ++i ) {
        LogTypeConfig logType;
        logType.name = rules[ i ].name.empty() ? "Default" : rules[ i ].name;
        logType.globPattern = rules[ i ].fileGlobPattern.empty() ? "*" : rules[ i ].fileGlobPattern;
        logType.parsingRule = rules[ i ];
        logType.isDefault = false;
        logTypes_.push_back( logType );
    }

    // Ensure at least one default log type exists.
    bool hasDefault = false;
    for ( const auto& logType : logTypes_ ) {
        if ( logType.isDefault ) {
            hasDefault = true;
            break;
        }
    }
    if ( !hasDefault && !logTypes_.empty() ) {
        logTypes_.front().isDefault = true;
    }
}

LogParsingRule* EventVisualizationManager::findParsingRuleForFile( const std::string& filePath )
{
    const LogTypeConfig* logType = findLogTypeForFile( filePath );
    if ( logType ) {
        // This is a const_cast, but we need it for backward compatibility
        return const_cast<LogParsingRule*>( &logType->parsingRule );
    }
    return nullptr;
}

const LogParsingRule* EventVisualizationManager::findParsingRuleForFile( const std::string& filePath ) const
{
    const LogTypeConfig* logType = findLogTypeForFile( filePath );
    if ( logType ) {
        return &logType->parsingRule;
    }
    return nullptr;
}

bool EventVisualizationManager::hasAnyParsingRule() const
{
    return hasAnyLogType();
}

const LogParsingRule* EventVisualizationManager::getDefaultParsingRule() const
{
    const LogTypeConfig* logType = getDefaultLogType();
    if ( logType ) {
        return &logType->parsingRule;
    }
    return nullptr;
}

std::vector<PerfettoEvent>
EventVisualizationManager::convertToPerfettoFormat( const std::vector<LogEvent>& events ) const
{
    std::vector<PerfettoEvent> perfettoEvents;

    // For async events: async_id -> Begin event
    std::map<std::string, const LogEvent*> pendingAsyncBegins;

    // For sync events: (pid, tid) -> stack of Begin events
    std::map<std::pair<int64_t, int64_t>, std::vector<const LogEvent*>> threadStacks;

    // Debug: count slice types
    int beginCount = 0, endCount = 0, noneCount = 0;

    for ( const auto& event : events ) {
        if ( event.eventType == EventType::Slice ) {
            if ( event.sliceType == SliceType::Begin ) {
                beginCount++;
            } else if ( event.sliceType == SliceType::End ) {
                endCount++;
            } else {
                noneCount++;
            }
        }
    }
    LOG_INFO << "convertToPerfettoFormat: " << events.size() << " events, Slice types: Begin="
             << beginCount << ", End=" << endCount << ", None=" << noneCount;

    for ( const auto& event : events ) {
        PerfettoEvent perfEvent;
        perfEvent.name = event.name;
        perfEvent.pid = event.pid > 0 ? event.pid : 0;
        perfEvent.tid = event.tid > 0 ? event.tid : 0;
        perfEvent.cat = event.category;

        // Build args
        std::map<std::string, std::string> args;
        for ( const auto& [ key, value ] : event.parameters ) {
            std::string paramValue = value;
            // Trim brackets
            while ( !paramValue.empty()
                    && ( paramValue.front() == '{' || paramValue.front() == '['
                         || paramValue.front() == '(' || paramValue.front() == '<' ) ) {
                paramValue.erase( 0, 1 );
            }
            while ( !paramValue.empty()
                    && ( paramValue.back() == '}' || paramValue.back() == ']'
                         || paramValue.back() == ')' || paramValue.back() == '>' ) ) {
                paramValue.pop_back();
            }
            args[ key ] = paramValue;
        }
        if ( !event.module.empty() ) {
            args[ "module" ] = event.module;
        }
        if ( !event.description.empty() ) {
            args[ "description" ] = event.description;
        }
        perfEvent.args = args;

        if ( event.eventType == EventType::Instance ) {
            // Instant event
            perfEvent.ph = "i";
            perfEvent.ts = event.timestamp;
            perfettoEvents.push_back( perfEvent );
        } else if ( event.eventType == EventType::Slice ) {
            if ( event.sliceType == SliceType::None ) {
                // Complete slice with duration
                perfEvent.ph = "X";
                perfEvent.ts = event.startTimestamp;
                perfEvent.dur = event.duration;
                perfettoEvents.push_back( perfEvent );
            } else if ( event.sliceType == SliceType::Begin ) {
                if ( !event.traceId.empty() ) {
                    // Async event: store for precise matching by async_id
                    pendingAsyncBegins[ event.traceId ] = &event;
                } else {
                    // Sync event: push to thread stack
                    auto threadKey = std::make_pair( perfEvent.pid, perfEvent.tid );
                    threadStacks[ threadKey ].push_back( &event );
                }
            } else if ( event.sliceType == SliceType::End ) {
                if ( !event.traceId.empty() ) {
                    // Async event: precise matching by async_id
                    auto pendingIt = pendingAsyncBegins.find( event.traceId );
                    if ( pendingIt != pendingAsyncBegins.end() ) {
                        const LogEvent* beginEvent = pendingIt->second;
                        pendingAsyncBegins.erase( pendingIt );

                        // Output X event (Complete) with duration
                        PerfettoEvent completePerf;
                        completePerf.name = beginEvent->name;
                        completePerf.ts = beginEvent->timestamp;
                        completePerf.pid = beginEvent->pid > 0 ? beginEvent->pid : 0;
                        completePerf.tid = beginEvent->tid > 0 ? beginEvent->tid : 0;
                        completePerf.ph = "X";
                        completePerf.cat = beginEvent->category;
                        completePerf.dur = event.timestamp - beginEvent->timestamp;

                        std::map<std::string, std::string> completeArgs;
                        for ( const auto& [ key, value ] : beginEvent->parameters ) {
                            completeArgs[ key ] = value;
                        }
                        // Merge end event parameters
                        for ( const auto& [ key, value ] : event.parameters ) {
                            if ( completeArgs.find( key ) == completeArgs.end() ) {
                                completeArgs[ key ] = value;
                            }
                        }
                        if ( !beginEvent->module.empty() ) {
                            completeArgs[ "module" ] = beginEvent->module;
                        }
                        completePerf.args = completeArgs;

                        perfettoEvents.push_back( completePerf );
                    }
                    // If no matching Begin found, skip this End
                } else {
                    // Sync event: pop from thread stack
                    auto threadKey = std::make_pair( perfEvent.pid, perfEvent.tid );
                    auto stackIt = threadStacks.find( threadKey );
                    if ( stackIt != threadStacks.end() && !stackIt->second.empty() ) {
                        const LogEvent* beginEvent = stackIt->second.back();
                        stackIt->second.pop_back();

                        // Clean up empty stacks
                        if ( stackIt->second.empty() ) {
                            threadStacks.erase( stackIt );
                        }

                        // Output X event (Complete) with duration
                        PerfettoEvent completePerf;
                        completePerf.name = beginEvent->name;
                        completePerf.ts = beginEvent->timestamp;
                        completePerf.pid = beginEvent->pid > 0 ? beginEvent->pid : 0;
                        completePerf.tid = beginEvent->tid > 0 ? beginEvent->tid : 0;
                        completePerf.ph = "X";
                        completePerf.cat = beginEvent->category;
                        completePerf.dur = event.timestamp - beginEvent->timestamp;

                        std::map<std::string, std::string> completeArgs;
                        for ( const auto& [ key, value ] : beginEvent->parameters ) {
                            completeArgs[ key ] = value;
                        }
                        // Merge end event parameters
                        for ( const auto& [ key, value ] : event.parameters ) {
                            if ( completeArgs.find( key ) == completeArgs.end() ) {
                                completeArgs[ key ] = value;
                            }
                        }
                        if ( !beginEvent->module.empty() ) {
                            completeArgs[ "module" ] = beginEvent->module;
                        }
                        completePerf.args = completeArgs;

                        perfettoEvents.push_back( completePerf );
                    }
                    // If no matching Begin found, skip this End
                }
            }
        }
    }

    // Output unpaired Begin events as open-ended Begin events.
    for ( const auto& pendingEntry : pendingAsyncBegins ) {
        const LogEvent* beginEvent = pendingEntry.second;
        PerfettoEvent beginPerf;
        beginPerf.name = beginEvent->name;
        beginPerf.ts = beginEvent->timestamp;
        beginPerf.pid = beginEvent->pid > 0 ? beginEvent->pid : 0;
        beginPerf.tid = beginEvent->tid > 0 ? beginEvent->tid : 0;
        beginPerf.ph = "B";
        beginPerf.cat = beginEvent->category;
        beginPerf.id = beginEvent->traceId;

        std::map<std::string, std::string> beginArgs;
        for ( const auto& [ key, value ] : beginEvent->parameters ) {
            beginArgs[ key ] = value;
        }
        if ( !beginEvent->module.empty() ) {
            beginArgs[ "module" ] = beginEvent->module;
        }
        beginPerf.args = beginArgs;
        perfettoEvents.push_back( beginPerf );
    }

    for ( const auto& stackEntry : threadStacks ) {
        for ( const LogEvent* beginEvent : stackEntry.second ) {
            PerfettoEvent beginPerf;
            beginPerf.name = beginEvent->name;
            beginPerf.ts = beginEvent->timestamp;
            beginPerf.pid = beginEvent->pid > 0 ? beginEvent->pid : 0;
            beginPerf.tid = beginEvent->tid > 0 ? beginEvent->tid : 0;
            beginPerf.ph = "B";
            beginPerf.cat = beginEvent->category;

            std::map<std::string, std::string> beginArgs;
            for ( const auto& [ key, value ] : beginEvent->parameters ) {
                beginArgs[ key ] = value;
            }
            if ( !beginEvent->module.empty() ) {
                beginArgs[ "module" ] = beginEvent->module;
            }
            beginPerf.args = beginArgs;
            perfettoEvents.push_back( beginPerf );
        }
    }

    return perfettoEvents;
}

std::string EventVisualizationManager::serializeConfiguration() const
{
    nlohmann::json root;

    // Save log types
    nlohmann::json logTypesArray = nlohmann::json::array();
    for ( const auto& logType : logTypes_ ) {
        nlohmann::json logTypeObj;
        logTypeObj[ "name" ] = logType.name;
        logTypeObj[ "globPattern" ] = logType.globPattern;
        logTypeObj[ "enabled" ] = logType.enabled;
        logTypeObj[ "isDefault" ] = logType.isDefault;

        // Save parsing rule
        nlohmann::json parsingObj;
        parsingObj[ "name" ] = logType.parsingRule.name;
        parsingObj[ "fileGlobPattern" ] = logType.parsingRule.fileGlobPattern;
        parsingObj[ "parsePattern" ] = logType.parsingRule.parsePattern;
        parsingObj[ "timestampFormat" ] = logType.parsingRule.timestampFormat;
        parsingObj[ "enabled" ] = logType.parsingRule.enabled;
        logTypeObj[ "parsingRule" ] = parsingObj;

        // Save event rules by category
        nlohmann::json eventRulesObj = nlohmann::json::object();
        for ( const auto& [ category, categoryRules ] : logType.eventRulesByCategory ) {
            nlohmann::json categoryArray = nlohmann::json::array();
            for ( const auto& rule : categoryRules ) {
                nlohmann::json eventObj;
                eventObj[ "name" ] = rule.name;
                eventObj[ "description" ] = rule.description;
                eventObj[ "enabled" ] = rule.enabled;
                eventObj[ "eventType" ] = eventTypeToString( rule.eventType );

                if ( rule.eventType == EventType::Instance ) {
                    eventObj[ "pattern" ] = rule.pattern;
                } else {
                    eventObj[ "beginPattern" ] = rule.beginPattern;
                    eventObj[ "endPattern" ] = rule.endPattern;
                    if ( !rule.asyncIdParam.empty() ) {
                        eventObj[ "asyncIdParam" ] = rule.asyncIdParam;
                    }
                    if ( !rule.durationParam.empty() ) {
                        eventObj[ "durationParam" ] = rule.durationParam;
                    }
                    if ( rule.durationUnit != DurationUnit::Ms ) {
                        eventObj[ "durationUnit" ] = durationUnitToString( rule.durationUnit );
                    }
                }

                nlohmann::json paramArray = nlohmann::json::array();
                for ( const auto& param : rule.parameterNames ) {
                    paramArray.push_back( param );
                }
                eventObj[ "parameters" ] = paramArray;

                if ( !rule.endParameterNames.empty() ) {
                    nlohmann::json endParamArray = nlohmann::json::array();
                    for ( const auto& param : rule.endParameterNames ) {
                        endParamArray.push_back( param );
                    }
                    eventObj[ "endParameters" ] = endParamArray;
                }

                categoryArray.push_back( eventObj );
            }
            eventRulesObj[ category ] = categoryArray;
        }
        logTypeObj[ "eventRules" ] = eventRulesObj;

        logTypesArray.push_back( logTypeObj );
    }
    root[ "logTypes" ] = logTypesArray;

    return root.dump( 4 );
}

ConfigurationLoadResult
EventVisualizationManager::deserializeConfiguration( const std::string& json )
{
    ConfigurationLoadResult result;

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse( json );
    } catch ( const nlohmann::json::parse_error& e ) {
        result.errorMessage
            = "JSON parse error at position " + std::to_string( e.byte ) + ": " + e.what();
        LOG_ERROR << "Failed to parse configuration JSON: " << e.what() << " at offset " << e.byte;
        return result;
    }

    if ( !doc.is_object() ) {
        result.errorMessage = "Configuration file must be a JSON object";
        LOG_ERROR << "Configuration JSON is not an object";
        return result;
    }

    nlohmann::json root = doc;

    // Load log types
    logTypes_.clear();
    int totalEventRules = 0;

    if ( jsonMember( root, "logTypes" ) ) {
        nlohmann::json logTypesArray = jsonArray( root, "logTypes" );
        for ( const auto& logTypeValue : logTypesArray ) {
            if ( !logTypeValue.is_object() ) {
                result.warnings.push_back( "Invalid logType entry, skipped" );
                continue;
            }

            nlohmann::json logTypeObj = logTypeValue;

            LogTypeConfig logType;
            logType.name = jsonString( logTypeObj, "name" );
            logType.globPattern = jsonString( logTypeObj, "globPattern" );
            logType.enabled = jsonBool( logTypeObj, "enabled", true );
            logType.isDefault = jsonBool( logTypeObj, "isDefault", false );

            // Load parsing rule
            if ( jsonMember( logTypeObj, "parsingRule" ) ) {
                nlohmann::json parsingObj = jsonObject( logTypeObj, "parsingRule" );
                logType.parsingRule.name = jsonString( parsingObj, "name" );
                // Optional key: configs written before it existed load with an empty glob
                logType.parsingRule.fileGlobPattern = jsonString( parsingObj, "fileGlobPattern" );
                logType.parsingRule.parsePattern = jsonString( parsingObj, "parsePattern" );
                logType.parsingRule.timestampFormat
                    = jsonString( parsingObj, "timestampFormat" );
                logType.parsingRule.enabled = jsonBool( parsingObj, "enabled", true );

                // Validate parsing pattern
                if ( !logType.parsingRule.parsePattern.empty() ) {
                    Pcre2Regex testRegex( logType.parsingRule.parsePattern );
                    if ( !testRegex.isValid() ) {
                        result.warnings.push_back( "Invalid parsePattern in '" + logType.name
                                                   + "': " + testRegex.errorString() );
                    }
                }
            }

            // Load event rules by category
            if ( jsonMember( logTypeObj, "eventRules" ) ) {
                nlohmann::json eventRulesObj = jsonObject( logTypeObj, "eventRules" );
                for ( auto categoryIt = eventRulesObj.begin(); categoryIt != eventRulesObj.end();
                      ++categoryIt ) {
                    std::string category = categoryIt.key();
                    nlohmann::json eventArray = categoryIt.value();
                    std::vector<EventRule> categoryRules;

                    if ( eventArray.is_array() ) {
                        for ( const auto& eventValue : eventArray ) {
                            if ( !eventValue.is_object() ) {
                                result.warnings.push_back( "Invalid event rule in category '"
                                                           + category + "', skipped" );
                                continue;
                            }

                            nlohmann::json eventObj = eventValue;
                            EventRule rule;
                            rule.name = jsonString( eventObj, "name" );
                            rule.category = category;
                            rule.description = jsonString( eventObj, "description" );
                            rule.enabled = jsonBool( eventObj, "enabled", true );
                            rule.eventType = parseEventType( jsonMember( eventObj, "eventType" )
                                                                 ? *jsonMember( eventObj,
                                                                                "eventType" )
                                                                 : nlohmann::json( nullptr ) );

                            bool ruleValid = true;

                            if ( rule.eventType == EventType::Instance ) {
                                rule.pattern = jsonString( eventObj, "pattern" );
                                if ( rule.pattern.empty() ) {
                                    result.warnings.push_back(
                                        "Event '" + rule.name + "' has empty pattern" );
                                    ruleValid = false;
                                } else {
                                    Pcre2Regex testRegex( rule.pattern );
                                    if ( !testRegex.isValid() ) {
                                        result.warnings.push_back(
                                            "Event '" + rule.name
                                            + "' has invalid pattern: "
                                            + testRegex.errorString() );
                                        ruleValid = false;
                                    }
                                }
                            } else {
                                rule.beginPattern = jsonString( eventObj, "beginPattern" );
                                rule.endPattern = jsonString( eventObj, "endPattern" );
                                rule.asyncIdParam = jsonString( eventObj, "asyncIdParam" );
                                rule.durationParam = jsonString( eventObj, "durationParam" );
                                rule.durationUnit
                                    = parseDurationUnit( jsonMember( eventObj, "durationUnit" )
                                                             ? *jsonMember( eventObj,
                                                                            "durationUnit" )
                                                             : nlohmann::json( nullptr ) );

                                // Validate slice patterns
                                if ( rule.beginPattern.empty() ) {
                                    result.warnings.push_back( "Slice event '" + rule.name
                                                               + "' has empty beginPattern" );
                                    ruleValid = false;
                                } else {
                                    Pcre2Regex testRegex( rule.beginPattern );
                                    if ( !testRegex.isValid() ) {
                                        result.warnings.push_back(
                                            "Slice '" + rule.name
                                            + "' has invalid beginPattern: "
                                            + testRegex.errorString() );
                                        ruleValid = false;
                                    }
                                }

                                if ( !rule.endPattern.empty() ) {
                                    Pcre2Regex testRegex( rule.endPattern );
                                    if ( !testRegex.isValid() ) {
                                        result.warnings.push_back(
                                            "Slice '" + rule.name
                                            + "' has invalid endPattern: "
                                            + testRegex.errorString() );
                                        ruleValid = false;
                                    }
                                }

                                // Validate asyncIdParam exists in both patterns
                                if ( !rule.asyncIdParam.empty() && !rule.beginPattern.empty()
                                     && !rule.endPattern.empty() ) {
                                    Pcre2Regex beginRegex( rule.beginPattern );
                                    Pcre2Regex endRegex( rule.endPattern );
                                    const auto beginGroups = beginRegex.namedCaptureGroups();
                                    const auto endGroups = endRegex.namedCaptureGroups();
                                    bool inBegin
                                        = std::find( beginGroups.begin(), beginGroups.end(),
                                                     rule.asyncIdParam )
                                        != beginGroups.end();
                                    bool inEnd = std::find( endGroups.begin(), endGroups.end(),
                                                            rule.asyncIdParam )
                                        != endGroups.end();
                                    if ( !inBegin || !inEnd ) {
                                        result.warnings.push_back(
                                            "Slice '" + rule.name + "': asyncIdParam '"
                                            + rule.asyncIdParam
                                            + "' not found in both patterns (begin: "
                                            + ( inBegin ? "yes" : "no" ) + ", end: "
                                            + ( inEnd ? "yes" : "no" ) + ")" );
                                    }
                                }
                            }

                            nlohmann::json paramArray = jsonArray( eventObj, "parameters" );
                            for ( const auto& paramValue : paramArray ) {
                                rule.parameterNames.push_back( paramValue.is_string()
                                                                   ? paramValue.get<std::string>()
                                                                   : "" );
                            }

                            nlohmann::json endParamArray = jsonArray( eventObj, "endParameters" );
                            for ( const auto& paramValue : endParamArray ) {
                                rule.endParameterNames.push_back( paramValue.is_string()
                                                                      ? paramValue
                                                                            .get<std::string>()
                                                                      : "" );
                            }

                            if ( ruleValid ) {
                                categoryRules.push_back( rule );
                                totalEventRules++;
                            }
                        }
                    }

                    logType.eventRulesByCategory[ category ] = categoryRules;
                }
            }

            logTypes_.push_back( logType );
        }
    }

    result.success = true;
    result.logTypeCount = static_cast<int>( logTypes_.size() );
    result.eventRuleCount = totalEventRules;

    LOG_INFO << "Loaded configuration (" << logTypes_.size() << " log types, " << totalEventRules
             << " event rules)";

    return result;
}

std::vector<LogEvent>
EventVisualizationManager::extractEventsFromFile( const std::string& filename,
                                                  const std::vector<EventRule>& rules )
{
    std::vector<LogEvent> events;

    LOG_INFO << "extractEventsFromFile: filename=" << filename << ", rules=" << rules.size();

    std::ifstream file( filename, std::ios::binary );
    if ( !file.is_open() ) {
        LOG_ERROR << "Could not open log file: " << filename;
        return events;
    }

    // Pre-compile all regex patterns for performance
    // Avoids creating Pcre2Regex inside the loop (N lines x M rules)
    struct CompiledRule {
        EventRule rule;
        Pcre2Regex patternRegex;
        Pcre2Regex beginRegex;
        Pcre2Regex endRegex;
    };

    std::vector<CompiledRule> compiledRules;
    compiledRules.reserve( rules.size() );

    int enabledRules = 0;
    for ( const auto& rule : rules ) {
        if ( !rule.enabled ) {
            continue;
        }
        enabledRules++;

        CompiledRule cr;
        cr.rule = rule;

        if ( rule.eventType == EventType::Instance && !rule.pattern.empty() ) {
            cr.patternRegex = Pcre2Regex( rule.pattern, Pcre2Regex::CaseInsensitiveOption );
            LOG_DEBUG << "Compiled Instance rule: " << rule.name
                      << ", pattern=" << rule.pattern.substr( 0, 50 ) << "...";
        } else {
            if ( !rule.beginPattern.empty() ) {
                cr.beginRegex = Pcre2Regex( rule.beginPattern, Pcre2Regex::CaseInsensitiveOption );
            }
            if ( !rule.endPattern.empty() ) {
                cr.endRegex = Pcre2Regex( rule.endPattern, Pcre2Regex::CaseInsensitiveOption );
            }

            // Validate asyncIdParam exists in both patterns
            if ( !rule.asyncIdParam.empty() ) {
                const auto beginGroups = cr.beginRegex.namedCaptureGroups();
                const auto endGroups = cr.endRegex.namedCaptureGroups();
                bool inBegin = std::find( beginGroups.begin(), beginGroups.end(),
                                          rule.asyncIdParam )
                    != beginGroups.end();
                bool inEnd = std::find( endGroups.begin(), endGroups.end(), rule.asyncIdParam )
                    != endGroups.end();
                if ( !inBegin || !inEnd ) {
                    LOG_WARNING << "Event rule '" << rule.name << "': asyncIdParam '"
                                << rule.asyncIdParam << "' not found in "
                                << ( !inBegin ? "beginPattern" : "endPattern" )
                                << ". Begin-End matching will fail.";
                }
            }
        }
        compiledRules.push_back( std::move( cr ) );
    }

    LOG_INFO << "extractEventsFromFile: " << enabledRules << " enabled rules, "
             << compiledRules.size() << " compiled rules";

    if ( compiledRules.empty() ) {
        LOG_WARNING << "extractEventsFromFile: No compiled rules to search with!";
        return events;
    }

    const LogParsingRule* parsingRule = findParsingRuleForFile( filename );
    if ( parsingRule ) {
        LOG_INFO << "extractEventsFromFile: Using parsing rule: " << parsingRule->name;
    } else {
        LOG_WARNING << "extractEventsFromFile: No parsing rule found, timestamps may be missing!";
    }

    std::string line;
    LineNumber lineNumber = 0_lnum;
    bool firstLine = true;

    while ( std::getline( file, line ) ) {
        ++lineNumber;

        if ( !line.empty() && line.back() == '\r' ) {
            line.pop_back();
        }
        if ( firstLine && line.size() >= 3
             && line.compare( 0, 3, "\xEF\xBB\xBF" ) == 0 ) {
            line.erase( 0, 3 );
        }
        firstLine = false;

        for ( const auto& cr : compiledRules ) {
            if ( cr.rule.eventType == EventType::Instance ) {
                // Instance type: use single pattern
                Pcre2RegexMatch match = cr.patternRegex.match( line );

                if ( match.hasMatch() ) {
                    LogEvent event = parseLine( line, cr.rule, lineNumber, parsingRule );
                    events.push_back( event );
                }
            } else {
                // Slice type: try begin pattern first, then end pattern
                bool matched = false;

                if ( cr.beginRegex.isValid() ) {
                    Pcre2RegexMatch beginMatch = cr.beginRegex.match( line );

                    if ( beginMatch.hasMatch() ) {
                        LogEvent event = parseLine( line, cr.rule, lineNumber, parsingRule );
                        event.sliceType = cr.rule.durationParam.empty() ? SliceType::Begin
                                                                         : SliceType::None;
                        event.pattern = cr.rule.beginPattern;
                        LOG_DEBUG << "extractEventsFromFile: Matched BEGIN pattern for rule "
                                  << cr.rule.name << ", line " << lineNumber.get()
                                  << ", sliceType=" << static_cast<int>( event.sliceType );
                        events.push_back( event );
                        matched = true;
                    }
                }

                if ( !matched && cr.endRegex.isValid() ) {
                    Pcre2RegexMatch endMatch = cr.endRegex.match( line );

                    if ( endMatch.hasMatch() ) {
                        LogEvent event = parseLine( line, cr.rule, lineNumber, parsingRule );
                        event.sliceType = SliceType::End;
                        event.pattern = cr.rule.endPattern;
                        LOG_DEBUG << "extractEventsFromFile: Matched END pattern for rule "
                                  << cr.rule.name << ", line " << lineNumber.get()
                                  << ", sliceType=" << static_cast<int>( event.sliceType );
                        events.push_back( event );
                    }
                }
            }
        }
    }

    return events;
}

std::vector<LogEvent> EventVisualizationManager::extractEventsFromSearchResults(
    const LogData* logData, const std::vector<LineNumber>& matchingLines,
    const std::vector<EventRule>& rules )
{
    const LogParsingRule* parsingRule = nullptr;
    for ( const auto& logType : logTypes_ ) {
        if ( logType.enabled && logType.parsingRule.enabled ) {
            parsingRule = &logType.parsingRule;
            break;
        }
    }
    return extractEventsFromSearchResults( logData, matchingLines, rules, parsingRule );
}

std::vector<LogEvent> EventVisualizationManager::extractEventsFromSearchResults(
    const LogData* logData, const std::vector<LineNumber>& matchingLines,
    const std::vector<EventRule>& rules, const LogParsingRule* parsingRule )
{
    std::vector<LogEvent> events;

    if ( !logData || matchingLines.empty() || rules.empty() ) {
        return events;
    }

    // Pre-compile all regex patterns
    struct CompiledRule {
        EventRule rule;
        Pcre2Regex patternRegex;
        Pcre2Regex beginRegex;
        Pcre2Regex endRegex;
    };

    std::vector<CompiledRule> compiledRules;
    compiledRules.reserve( rules.size() );

    for ( const auto& rule : rules ) {
        if ( !rule.enabled ) {
            continue;
        }

        CompiledRule cr;
        cr.rule = rule;

        if ( rule.eventType == EventType::Instance && !rule.pattern.empty() ) {
            cr.patternRegex = Pcre2Regex( rule.pattern, Pcre2Regex::CaseInsensitiveOption );
        } else {
            if ( !rule.beginPattern.empty() ) {
                cr.beginRegex = Pcre2Regex( rule.beginPattern, Pcre2Regex::CaseInsensitiveOption );
            }
            if ( !rule.endPattern.empty() ) {
                cr.endRegex = Pcre2Regex( rule.endPattern, Pcre2Regex::CaseInsensitiveOption );
            }

            // Validate asyncIdParam exists in both patterns
            if ( !rule.asyncIdParam.empty() ) {
                const auto beginGroups = cr.beginRegex.namedCaptureGroups();
                const auto endGroups = cr.endRegex.namedCaptureGroups();
                bool inBegin = std::find( beginGroups.begin(), beginGroups.end(),
                                          rule.asyncIdParam )
                    != beginGroups.end();
                bool inEnd = std::find( endGroups.begin(), endGroups.end(), rule.asyncIdParam )
                    != endGroups.end();
                if ( !inBegin || !inEnd ) {
                    LOG_WARNING << "Event rule '" << rule.name << "': asyncIdParam '"
                                << rule.asyncIdParam << "' not found in "
                                << ( !inBegin ? "beginPattern" : "endPattern" )
                                << ". Begin-End matching will fail.";
                }
            }
        }
        compiledRules.push_back( std::move( cr ) );
    }

    if ( compiledRules.empty() ) {
        return events;
    }

    // Process matching lines in batches
    constexpr int batchSize = 1000;
    const int totalLines = static_cast<int>( matchingLines.size() );

    for ( int batchStart = 0; batchStart < totalLines; batchStart += batchSize ) {
        const int batchEnd = std::min( batchStart + batchSize, totalLines );

        // Collect line numbers for this batch
        std::vector<LineNumber> batchLineNumbers;
        batchLineNumbers.reserve( static_cast<size_t>( batchEnd - batchStart ) );
        for ( int i = batchStart; i < batchEnd; ++i ) {
            batchLineNumbers.push_back( matchingLines[ static_cast<size_t>( i ) ] );
        }

        // Get lines in batch
        // Note: LogData doesn't have a batch get by line numbers, so we get them individually
        // but we can still batch the regex matching
        for ( const LineNumber lineNumber : batchLineNumbers ) {
            const std::string line = logData->getLineString( lineNumber );

            for ( const auto& cr : compiledRules ) {
                if ( cr.rule.eventType == EventType::Instance ) {
                    Pcre2RegexMatch match = cr.patternRegex.match( line );
                    if ( match.hasMatch() ) {
                        LogEvent event = parseLine( line, cr.rule, lineNumber, parsingRule );
                        events.push_back( event );
                        break;  // One line matches one rule
                    }
                } else {
                    // Slice type
                    bool matched = false;

                    if ( !cr.beginRegex.pattern().empty() ) {
                        Pcre2RegexMatch beginMatch = cr.beginRegex.match( line );
                        if ( beginMatch.hasMatch() ) {
                            LogEvent event = parseLine( line, cr.rule, lineNumber, parsingRule );
                            event.sliceType = cr.rule.durationParam.empty() ? SliceType::Begin
                                                                             : SliceType::None;
                            event.pattern = cr.rule.beginPattern;
                            events.push_back( event );
                            matched = true;
                            break;
                        }
                    }

                    if ( !matched && !cr.endRegex.pattern().empty() ) {
                        Pcre2RegexMatch endMatch = cr.endRegex.match( line );
                        if ( endMatch.hasMatch() ) {
                            LogEvent event = parseLine( line, cr.rule, lineNumber, parsingRule );
                            event.sliceType = SliceType::End;
                            event.pattern = cr.rule.endPattern;
                            events.push_back( event );
                            break;
                        }
                    }
                }
            }
        }
    }

    LOG_INFO << "extractEventsFromSearchResults: found " << events.size() << " events from "
             << matchingLines.size() << " matching lines";

    return events;
}

std::vector<SliceLineRange>
EventVisualizationManager::buildSliceRanges( const std::vector<LogEvent>& events,
                                             LineNumber lastLine ) const
{
    std::vector<SliceLineRange> ranges;

    if ( events.empty() ) {
        return ranges;
    }

    std::map<std::string, const LogEvent*> pendingAsyncBegins;
    std::map<std::string, std::vector<const LogEvent*>> threadStacks;

    auto threadKeyFor = []( const LogEvent& event ) {
        return std::to_string( event.pid ) + "|" + std::to_string( event.tid ) + "|"
            + event.category + "|" + event.name;
    };

    auto appendRange = [ &ranges ]( const LogEvent& beginEvent, const LogEvent& endEvent,
                                    bool openEnded ) {
        SliceLineRange range;
        range.ruleName = beginEvent.name;
        range.category = beginEvent.category;
        range.description = beginEvent.description;
        range.beginLine = beginEvent.line;
        range.endLine = endEvent.line;
        range.startTimestamp = beginEvent.timestamp;
        range.endTimestamp = endEvent.timestamp;
        range.duration = std::max<int64_t>( 0, endEvent.timestamp - beginEvent.timestamp );
        range.pid = beginEvent.pid;
        range.tid = beginEvent.tid;
        range.traceId = beginEvent.traceId;
        range.openEnded = openEnded;
        ranges.push_back( range );
    };

    auto appendOpenEndedRange = [ &ranges, lastLine ]( const LogEvent& beginEvent ) {
        SliceLineRange range;
        range.ruleName = beginEvent.name;
        range.category = beginEvent.category;
        range.description = beginEvent.description;
        range.beginLine = beginEvent.line;
        range.endLine = lastLine;
        range.startTimestamp = beginEvent.timestamp;
        range.endTimestamp = 0;
        range.duration = 0;
        range.pid = beginEvent.pid;
        range.tid = beginEvent.tid;
        range.traceId = beginEvent.traceId;
        range.openEnded = true;
        ranges.push_back( range );
    };

    for ( const auto& event : events ) {
        if ( event.eventType != EventType::Slice ) {
            continue;
        }

        if ( event.sliceType == SliceType::None ) {
            SliceLineRange range;
            range.ruleName = event.name;
            range.category = event.category;
            range.description = event.description;
            range.beginLine = event.line;
            range.endLine = event.line;
            range.startTimestamp = event.startTimestamp;
            range.endTimestamp = event.timestamp;
            range.duration = event.duration;
            range.pid = event.pid;
            range.tid = event.tid;
            range.traceId = event.traceId;
            range.openEnded = false;
            ranges.push_back( range );
            continue;
        }

        if ( event.sliceType == SliceType::Begin ) {
            if ( !event.traceId.empty() ) {
                pendingAsyncBegins[ event.name + "|" + event.traceId ] = &event;
            } else {
                threadStacks[ threadKeyFor( event ) ].push_back( &event );
            }
            continue;
        }

        if ( event.sliceType == SliceType::End ) {
            if ( !event.traceId.empty() ) {
                const std::string key = event.name + "|" + event.traceId;
                auto pendingIt = pendingAsyncBegins.find( key );
                if ( pendingIt != pendingAsyncBegins.end() ) {
                    const LogEvent* beginEvent = pendingIt->second;
                    pendingAsyncBegins.erase( pendingIt );
                    appendRange( *beginEvent, event, false );
                }
            } else {
                const std::string key = threadKeyFor( event );
                auto stackIt = threadStacks.find( key );
                if ( stackIt != threadStacks.end() && !stackIt->second.empty() ) {
                    const LogEvent* beginEvent = stackIt->second.back();
                    stackIt->second.pop_back();
                    appendRange( *beginEvent, event, false );
                    if ( stackIt->second.empty() ) {
                        threadStacks.erase( stackIt );
                    }
                }
            }
        }
    }

    for ( const auto& pendingEntry : pendingAsyncBegins ) {
        appendOpenEndedRange( *pendingEntry.second );
    }

    for ( const auto& stackEntry : threadStacks ) {
        for ( const LogEvent* beginEvent : stackEntry.second ) {
            appendOpenEndedRange( *beginEvent );
        }
    }

    std::sort( ranges.begin(), ranges.end(),
               []( const SliceLineRange& lhs, const SliceLineRange& rhs ) {
                   if ( lhs.beginLine != rhs.beginLine ) {
                       return lhs.beginLine < rhs.beginLine;
                   }
                   if ( lhs.endLine != rhs.endLine ) {
                       return lhs.endLine < rhs.endLine;
                   }
                   return lhs.ruleName < rhs.ruleName;
               } );

    return ranges;
}

std::unique_ptr<EventExporter> EventVisualizationManager::createExporter( ExportFormat format )
{
    switch ( format ) {
    case ExportFormat::Json:
        return std::make_unique<JsonExporter>();
    case ExportFormat::BinaryProto:
        return std::make_unique<BinaryProtoExporter>();
    default:
        return std::make_unique<JsonExporter>();
    }
}

std::vector<std::pair<ExportFormat, std::string>>
EventVisualizationManager::getSupportedFormats()
{
    return { { ExportFormat::Json, "Perfetto JSON (*.json)" },
             { ExportFormat::BinaryProto,
               "Perfetto Binary (*.perfetto) - Coming Soon" } };
}

bool JsonExporter::exportToFile( const std::string& filename,
                                 const std::vector<PerfettoEvent>& events ) const
{
    nlohmann::json traceObject;
    nlohmann::json traceEventsArray = nlohmann::json::array();

    for ( const auto& perfEvent : events ) {
        nlohmann::json eventObj;
        eventObj[ "name" ] = perfEvent.name;
        eventObj[ "ts" ] = perfEvent.ts / 1000LL;  // ns -> us (Chrome JSON format)
        eventObj[ "pid" ] = perfEvent.pid;
        eventObj[ "tid" ] = perfEvent.tid;
        eventObj[ "ph" ] = perfEvent.ph;
        eventObj[ "cat" ] = perfEvent.cat;
        eventObj[ "args" ] = perfEvent.args;

        if ( perfEvent.ph == "X" ) {
            eventObj[ "dur" ] = perfEvent.dur / 1000LL;  // ns -> us
        }

        if ( ( perfEvent.ph == "B" || perfEvent.ph == "E" ) && !perfEvent.id.empty() ) {
            eventObj[ "id" ] = perfEvent.id;
        }

        traceEventsArray.push_back( eventObj );
    }

    traceObject[ "traceEvents" ] = traceEventsArray;

    nlohmann::json metadata;
    metadata[ "created_by" ] = "Klogg";
    traceObject[ "metadata" ] = metadata;

    std::ofstream file( filename );
    if ( file.is_open() ) {
        file << traceObject.dump( 4 ) << '\n';
        return true;
    }

    return false;
}

bool BinaryProtoExporter::exportToFile( const std::string& filename,
                                        const std::vector<PerfettoEvent>& events ) const
{
    // Perfetto binary protobuf export is not implemented; callers get an error
    // (the API reports InternalError for this format)

    (void)filename;
    (void)events;

    LOG_WARNING << "Binary protobuf export not yet implemented";
    return false;
}

EventVisualizationManager::ExportResult EventVisualizationManager::exportToPerfetto(
    const std::string& sourceFileName, const std::vector<EventRule>& rules,
    const LogData* logData, const std::vector<LineNumber>& matchingLines )
{
    ExportResult result;

    LOG_INFO << "exportToPerfetto: sourceFileName=" << sourceFileName << ", rules=" << rules.size()
             << ", matchingLines=" << matchingLines.size();

    // Find parsing rule for this file
    const LogParsingRule* parsingRule = findParsingRuleForFile( sourceFileName );
    if ( parsingRule ) {
        LOG_INFO << "exportToPerfetto: Found parsing rule: " << parsingRule->name
                 << ", pattern=" << parsingRule->parsePattern.substr( 0, 50 ) << "...";
    } else {
        LOG_WARNING
            << "exportToPerfetto: No parsing rule found for file, timestamps may be missing!";
    }

    // Extract events
    std::vector<LogEvent> events;
    if ( logData && !matchingLines.empty() ) {
        LOG_INFO << "exportToPerfetto: Using extractEventsFromSearchResults";
        events = extractEventsFromSearchResults( logData, matchingLines, rules );
    }

    if ( events.empty() ) {
        LOG_INFO << "exportToPerfetto: Falling back to extractEventsFromFile";
        events = extractEventsFromFile( sourceFileName, rules );
    }

    LOG_INFO << "exportToPerfetto: Extracted " << events.size() << " events";

    if ( events.empty() ) {
        result.errorMessage = "No events found in the current file.\n\n"
            "Possible reasons:\n"
            "1. No event rules match any lines in this file\n"
            "2. Event rules are disabled\n"
            "3. The file format doesn't match the configured patterns\n\n"
            "Check Tools > Log Parsing Rules and Manage Event Rules.";
        return result;
    }

    // Filter events with valid timestamps
    std::vector<LogEvent> validEvents;
    for ( const auto& event : events ) {
        if ( event.hasValidTimestamp ) {
            validEvents.push_back( event );
        } else {
            result.skippedCount++;
        }
    }

    LOG_INFO << "exportToPerfetto: " << validEvents.size()
             << " events with valid timestamps, " << result.skippedCount << " skipped";

    if ( validEvents.empty() ) {
        result.errorMessage = "No events with valid timestamps found.\n\n"
            "Check your log parsing rules to ensure timestamps are extracted correctly.\n\n"
            "Supported formats:\n"
            "\u2022 yyyy-MM-ddTHH:mm:ss.zzz\n"
            "\u2022 yyyy-MM-dd HH:mm:ss\n"
            "\u2022 Unix timestamp (seconds or milliseconds)";
        return result;
    }

    // Convert to Perfetto format
    std::vector<PerfettoEvent> perfettoEvents = convertToPerfettoFormat( validEvents );

    int completeCount = 0;
    int orphanBeginCount = 0;
    int orphanEndCount = 0;
    for ( const auto& perfEvent : perfettoEvents ) {
        if ( perfEvent.ph == "X" ) {
            completeCount++;
        } else if ( perfEvent.ph == "B" ) {
            orphanBeginCount++;
        } else if ( perfEvent.ph == "E" ) {
            orphanEndCount++;
        }
    }

    if ( perfettoEvents.empty() ) {
        result.errorMessage = "No events could be exported to Perfetto.\n\n"
            "Check your event rules and log parsing rules.";
        return result;
    }

    // Export to file
    std::string baseName = std::filesystem::path( sourceFileName ).stem().string();

    std::string tempDir;
    const char* tempEnv = std::getenv( "TEMP" );
    if ( tempEnv == nullptr ) {
        tempEnv = std::getenv( "TMP" );
    }
    if ( tempEnv == nullptr ) {
        tempEnv = std::getenv( "TMPDIR" );
    }
    if ( tempEnv == nullptr ) {
        tempDir = "/tmp";
    } else {
        tempDir = tempEnv;
    }
    tempDir += "/klogg";

    std::error_code ec;
    std::filesystem::create_directories( tempDir, ec );

    std::time_t now = std::time( nullptr );
    std::tm localTm{};
#ifdef _WIN32
    localtime_s( &localTm, &now );
#else
    localtime_r( &now, &localTm );
#endif
    char timestampBuffer[ 32 ] = { 0 };
    std::strftime( timestampBuffer, sizeof( timestampBuffer ), "%Y%m%d_%H%M%S", &localTm );

    result.outputFile = tempDir + "/klogg_" + baseName + "_" + timestampBuffer + "_events.json";

    auto exporter = createExporter( ExportFormat::Json );
    if ( !exporter->exportToFile( result.outputFile, perfettoEvents ) ) {
        result.errorMessage = "Could not export events to file.";
        return result;
    }

    result.eventCount = static_cast<int>( perfettoEvents.size() );
    result.completeEventCount = completeCount;
    result.orphanBeginCount = orphanBeginCount;
    result.orphanEndCount = orphanEndCount;
    result.success = true;

    // Find trace_processor_shell
    const char separator =
#ifdef _WIN32
        ';';
#else
        ':';
#endif

    std::string traceProcessorPath;
    const char* pathValue = std::getenv( "PATH" );
    if ( pathValue != nullptr ) {
        std::string pathStr( pathValue );
        size_t start = 0;
        while ( start < pathStr.size() ) {
            size_t end = pathStr.find( separator, start );
            if ( end == std::string::npos ) {
                end = pathStr.size();
            }
            std::string dir = pathStr.substr( start, end - start );
#ifdef _WIN32
            if ( std::filesystem::exists( dir + "/trace_processor_shell.exe" ) ) {
                traceProcessorPath = dir + "/trace_processor_shell.exe";
                break;
            }
#else
            if ( std::filesystem::exists( dir + "/trace_processor_shell" ) ) {
                traceProcessorPath = dir + "/trace_processor_shell";
                break;
            }
#endif
            start = end + 1;
        }
    }

    if ( traceProcessorPath.empty() ) {
        std::string bundledPath = applicationDirPath() + "/trace_processor_shell";
#ifdef _WIN32
        bundledPath += ".exe";
#endif
        if ( std::filesystem::exists( bundledPath ) ) {
            traceProcessorPath = bundledPath;
        }
    }

    result.hasTraceProcessor = !traceProcessorPath.empty();

    return result;
}
