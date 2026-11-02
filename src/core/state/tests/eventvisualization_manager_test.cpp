/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

#include <catch2/catch.hpp>

#include "eventvisualization.h"
#include "linetypes.h"

#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTextStream>

#include <ctime>
#include <utility>
#include <vector>

namespace {

inline void resetEventVisualizationManager()
{
    auto& manager = EventVisualizationManager::getInstance();
    manager.setLogTypes( {} );
    manager.setEventRules( {} );
    manager.setLogParsingRules( {} );
}

struct EventVisualizationManagerResetGuard {
    EventVisualizationManagerResetGuard() { resetEventVisualizationManager(); }
    ~EventVisualizationManagerResetGuard() { resetEventVisualizationManager(); }
};

} // namespace

SCENARIO( "EventVisualizationManager regex generation", "[event]" )
{
    EventVisualizationManagerResetGuard reset;

    WHEN( "Text contains content in square brackets" )
    {
        const std::string text = "Request [abc123] started";
        const std::string pattern = EventVisualizationManager::generateRegexFromText( text );

        THEN( "Pattern is generated" )
        {
            REQUIRE( !pattern.empty() );
            REQUIRE( ( pattern.find( "\\" ) != std::string::npos
                       || pattern.find( "(?<" ) != std::string::npos ) );
        }
    }

    WHEN( "Text contains colon-separated value" )
    {
        const std::string text = "status : 200";
        const std::string pattern = EventVisualizationManager::generateRegexFromText( text );

        THEN( "Pattern captures value after colon" )
        {
            REQUIRE( !pattern.empty() );
            REQUIRE( pattern.find( "(?<" ) != std::string::npos );
        }
    }

    WHEN( "The word before a bracket starts with a digit" )
    {
        const std::string text = "2024-01-15 10:00:00.222 [ERROR] failed";
        const std::string pattern = EventVisualizationManager::generateRegexFromText( text );

        THEN( "The generated pattern is a valid regex" )
        {
            REQUIRE( !pattern.empty() );
            const QRegularExpression regex( QString::fromStdString( pattern ) );
            REQUIRE( regex.isValid() );
        }
    }
}

SCENARIO( "EventVisualizationManager event rule management", "[event]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    GIVEN( "An empty event rule set" )
    {
        manager.setEventRules( {} );

        WHEN( "Adding an Instance event rule" )
        {
            EventRule rule;
            rule.name = "test_instance";
            rule.category = "Test";
            rule.eventType = EventType::Instance;
            rule.pattern = "ERROR.*connection";
            rule.enabled = true;
            manager.addEventRule( rule );

            THEN( "Rule is stored and retrievable" )
            {
                auto rules = manager.getEventRules();
                REQUIRE( rules.size() == 1 );
                REQUIRE( rules[0].name == "test_instance" );
                REQUIRE( rules[0].eventType == EventType::Instance );
            }
        }

        WHEN( "Adding a Slice event rule" )
        {
            EventRule rule;
            rule.name = "test_slice";
            rule.category = "Test";
            rule.eventType = EventType::Slice;
            rule.beginPattern = "Request started.*id=(?<id>\\d+)";
            rule.endPattern = "Request completed.*id=(?<id>\\d+)";
            rule.asyncIdParam = "id";
            rule.enabled = true;
            manager.addEventRule( rule );

            THEN( "Slice rule is stored with begin and end patterns" )
            {
                auto rules = manager.getEventRules();
                REQUIRE( rules.size() == 1 );
                REQUIRE( rules[0].eventType == EventType::Slice );
            }
        }

        WHEN( "Updating an event rule" )
        {
            EventRule rule;
            rule.name = "update_test";
            rule.pattern = "old pattern";
            rule.enabled = true;
            manager.addEventRule( rule );

            EventRule updated;
            updated.name = "update_test";
            updated.pattern = "new pattern";
            updated.enabled = false;
            manager.updateEventRule( "update_test", updated );

            THEN( "Rule is updated" )
            {
                auto rules = manager.getEventRules();
                REQUIRE( rules[0].pattern == "new pattern" );
                REQUIRE( rules[0].enabled == false );
            }
        }

        WHEN( "Removing an event rule" )
        {
            EventRule rule;
            rule.name = "remove_test";
            rule.enabled = true;
            manager.addEventRule( rule );
            REQUIRE( manager.getEventRules().size() == 1 );

            manager.removeEventRule( "remove_test" );

            THEN( "Rule is removed" )
            {
                REQUIRE( manager.getEventRules().empty() );
            }
        }
    }
}

SCENARIO( "EventVisualizationManager log parsing rule", "[event][regression]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    GIVEN( "A log parsing rule" )
    {
        LogParsingRule rule;
        rule.name = "ISO Timestamp";
        rule.parsePattern = "^(?<timestamp>\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d{3})\\s+(?<level>\\w+)\\s+\\[(?<tid>\\d+)\\]\\s+(?<message>.*)$";
        rule.timestampFormat = "yyyy-MM-ddTHH:mm:ss.zzz";
        rule.enabled = true;

        manager.setLogParsingRules( { rule } );

        WHEN( "Parsing a log line" )
        {
            EventRule eventRule;
            eventRule.name = "test_event";
            eventRule.pattern = "test message";
            eventRule.eventType = EventType::Instance;
            eventRule.enabled = true;

            QString logLine = "2026-03-10T10:30:45.123 INFO [12345] test message here";

            const LogParsingRule* managerRule = manager.getDefaultParsingRule();
            REQUIRE( managerRule != nullptr );

            LogEvent event = manager.parseLine( logLine.toStdString(), eventRule, 0_lnum, managerRule );

            THEN( "Timestamp is extracted and converted to nanoseconds" )
            {
                REQUIRE( event.hasValidTimestamp );
                REQUIRE( event.timestamp > 0 );
            }

            AND_THEN( "TID is extracted" )
            {
                REQUIRE( event.tid == 12345 );
            }
        }
    }
}

SCENARIO( "setLogParsingRules preserves existing event rules", "[event][regression]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    GIVEN( "A log type containing event rules" )
    {
        LogTypeConfig logType;
        logType.name = "TestType";
        logType.globPattern = "*.log";
        logType.enabled = true;
        logType.isDefault = true;

        EventRule eventRule;
        eventRule.name = "existing_event";
        eventRule.category = "Cat";
        eventRule.eventType = EventType::Instance;
        eventRule.pattern = "EXISTING";
        eventRule.enabled = true;
        logType.eventRulesByCategory["Cat"] = { eventRule };

        manager.setLogTypes( { logType } );
        REQUIRE( manager.getEventRules().size() == 1 );

        WHEN( "Setting parsing rules for backward compatibility" )
        {
            LogParsingRule parseRule;
            parseRule.name = "ParserA";
            parseRule.fileGlobPattern = "*.klog";
            parseRule.parsePattern = "^(?<message>.*)$";
            parseRule.enabled = true;

            manager.setLogParsingRules( { parseRule } );

            THEN( "Existing event rules are still kept" )
            {
                auto rules = manager.getEventRules();
                REQUIRE( rules.size() == 1 );
                REQUIRE( rules[0].name == "existing_event" );
            }
        }
    }

    manager.setLogTypes( {} );
}

SCENARIO( "setLogParsingRules keeps file glob mapping", "[event][regression]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    GIVEN( "A parsing rule with explicit file glob" )
    {
        LogParsingRule parseRule;
        parseRule.name = "ParserWithGlob";
        parseRule.fileGlobPattern = "special_*.log";
        parseRule.parsePattern = "^(?<message>.*)$";
        parseRule.enabled = true;

        WHEN( "Applying parsing rule set" )
        {
            manager.setLogParsingRules( { parseRule } );

            THEN( "The created log type keeps the same glob pattern" )
            {
                const auto& logTypes = manager.getLogTypes();
                REQUIRE( logTypes.size() == 1 );
                REQUIRE( logTypes[0].globPattern == "special_*.log" );
            }
        }
    }

    manager.setLogTypes( {} );
}

SCENARIO( "parseLine extracts end-pattern parameters for slice end", "[event][regression]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    EventRule rule;
    rule.name = "slice_op";
    rule.category = "Ops";
    rule.eventType = EventType::Slice;
    rule.beginPattern = R"(BEGIN\s+(?<id>\w+))";
    rule.endPattern = R"(END\s+(?<id>\w+)\s+dur=(?<dur>\d+))";
    rule.parameterNames = { "id" };
    rule.endParameterNames = { "id", "dur" };
    rule.asyncIdParam = "id";
    rule.enabled = true;

    auto event = manager.parseLine( "END op42 dur=99", rule, 0_lnum, nullptr );

    REQUIRE( event.eventType == EventType::Slice );
    REQUIRE( event.sliceType == SliceType::End );
    REQUIRE( event.parameters.count( "id" ) != 0 );
    REQUIRE( event.parameters.count( "dur" ) != 0 );
    REQUIRE( event.parameters.at( "id" ) == "op42" );
    REQUIRE( event.parameters.at( "dur" ) == "99" );
}

SCENARIO( "sync slice begin and end pair without async id", "[event][regression]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    LogEvent beginEvent;
    beginEvent.name = "file_index";
    beginEvent.category = "Indexing";
    beginEvent.timestamp = 1000000000;
    beginEvent.pid = 1;
    beginEvent.tid = 42;
    beginEvent.eventType = EventType::Slice;
    beginEvent.sliceType = SliceType::Begin;
    beginEvent.hasValidTimestamp = true;
    beginEvent.parameters["file"] = "a.log";

    LogEvent endEvent;
    endEvent.name = "file_index";
    endEvent.category = "Indexing";
    endEvent.timestamp = 1500000000;
    endEvent.pid = 1;
    endEvent.tid = 42;
    endEvent.eventType = EventType::Slice;
    endEvent.sliceType = SliceType::End;
    endEvent.hasValidTimestamp = true;
    endEvent.parameters["duration"] = "500";

    auto perfettoEvents = manager.convertToPerfettoFormat( { beginEvent, endEvent } );

    REQUIRE( perfettoEvents.size() == 1 );
    REQUIRE( perfettoEvents[0].ph == "X" );
    REQUIRE( perfettoEvents[0].dur == 500000000 );
    REQUIRE( perfettoEvents[0].args.at( "file" ) == "a.log" );
    REQUIRE( perfettoEvents[0].args.at( "duration" ) == "500" );
}

SCENARIO( "unpaired slice begin is exported as open Perfetto begin event", "[event][regression]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    LogEvent beginEvent;
    beginEvent.name = "file_index";
    beginEvent.category = "Indexing";
    beginEvent.timestamp = 1000000000;
    beginEvent.pid = 1;
    beginEvent.tid = 42;
    beginEvent.eventType = EventType::Slice;
    beginEvent.sliceType = SliceType::Begin;
    beginEvent.hasValidTimestamp = true;
    beginEvent.parameters["file"] = "a.log";

    auto perfettoEvents = manager.convertToPerfettoFormat( { beginEvent } );

    REQUIRE( perfettoEvents.size() == 1 );
    REQUIRE( perfettoEvents[0].ph == "B" );
    REQUIRE( perfettoEvents[0].name == "file_index" );
}

SCENARIO( "exportToPerfetto succeeds with orphan begin events", "[event][regression]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    QTemporaryFile logFile;
    REQUIRE( logFile.open() );
    QTextStream stream( &logFile );
    stream << "2026-03-21T01:11:30.485 info [33176] [IndexOperation::doIndex@611] Indexing file \"sample.log\"\n";
    stream.flush();

    LogParsingRule parsingRule;
    parsingRule.name = "Klogg Log Format";
    parsingRule.fileGlobPattern = "*.log";
    parsingRule.parsePattern =
        "^(?<timestamp>\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d{3})\\s+"
        "(?<level>\\w+)\\s+\\[(?<tid>\\d+)\\]\\s+\\[(?<function>[^\\]]+)\\]\\s+(?<message>.*)$";
    parsingRule.timestampFormat = "yyyy-MM-ddTHH:mm:ss.zzz";
    parsingRule.enabled = true;
    manager.setLogParsingRules( { parsingRule } );

    EventRule rule;
    rule.name = "file_index";
    rule.category = "Indexing";
    rule.eventType = EventType::Slice;
    rule.beginPattern = "\\[IndexOperation::doIndex@\\d+\\].*Indexing file \"(?<file>[^\"]+)\"";
    rule.endPattern =
        "\\[IndexOperation::doIndex@\\d+\\].*Indexing done for \"(?<file>[^\"]+)\", took (?<duration>[\\d.]+) ms";
    rule.asyncIdParam = "file";
    rule.parameterNames = { "file" };
    rule.endParameterNames = { "file", "duration" };
    rule.enabled = true;

    auto result = manager.exportToPerfetto( logFile.fileName().toStdString(), { rule }, nullptr, {} );

    REQUIRE( result.success == true );
    REQUIRE( result.eventCount == 1 );
    REQUIRE( result.completeEventCount == 0 );
    REQUIRE( result.orphanBeginCount == 1 );
    REQUIRE( result.orphanEndCount == 0 );
}

SCENARIO( "EventVisualizationManager Perfetto format conversion", "[event]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    GIVEN( "Instance events" )
    {
        LogEvent event;
        event.name = "test_instant";
        event.category = "Test";
        event.timestamp = 1745905845000000;
        event.pid = 1;
        event.tid = 100;
        event.eventType = EventType::Instance;
        event.hasValidTimestamp = true;

        WHEN( "Converting to Perfetto format" )
        {
            std::vector<LogEvent> events = { event };
            auto perfettoEvents = manager.convertToPerfettoFormat( events );

            THEN( "Output is instant event (ph: i)" )
            {
                REQUIRE( perfettoEvents.size() == 1 );
                REQUIRE( perfettoEvents[0].ph == "i" );
                REQUIRE( perfettoEvents[0].name == "test_instant" );
            }
        }
    }

    GIVEN( "Slice events with Begin and End" )
    {
        LogEvent beginEvent;
        beginEvent.name = "test_slice";
        beginEvent.timestamp = 1745905845000000;
        beginEvent.pid = 1;
        beginEvent.tid = 100;
        beginEvent.eventType = EventType::Slice;
        beginEvent.sliceType = SliceType::Begin;
        beginEvent.hasValidTimestamp = true;
        beginEvent.traceId = "trace-123";

        LogEvent endEvent;
        endEvent.name = "test_slice";
        endEvent.timestamp = 1745905945000000;
        endEvent.pid = 1;
        endEvent.tid = 100;
        endEvent.eventType = EventType::Slice;
        endEvent.sliceType = SliceType::End;
        endEvent.hasValidTimestamp = true;
        endEvent.traceId = "trace-123";

        WHEN( "Converting paired slice to Perfetto format" )
        {
            std::vector<LogEvent> events = { beginEvent, endEvent };
            auto perfettoEvents = manager.convertToPerfettoFormat( events );

            THEN( "Output is complete slice event (ph: X) with duration" )
            {
                REQUIRE( perfettoEvents.size() == 1 );
                REQUIRE( perfettoEvents[0].ph == "X" );
                REQUIRE( perfettoEvents[0].dur == 100000000 );
            }
        }
    }
}

SCENARIO( "EventVisualizationManager configuration save and load", "[event][regression]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    GIVEN( "A log type configuration" )
    {
        LogTypeConfig logType;
        logType.name = "Test Log";
        logType.globPattern = "*.testlog";
        logType.enabled = true;

        EventRule eventRule;
        eventRule.name = "test_event";
        eventRule.category = "Test";
        eventRule.pattern = "test pattern";
        eventRule.eventType = EventType::Instance;
        eventRule.enabled = true;
        logType.eventRulesByCategory["Test"] = { eventRule };

        manager.setLogTypes( { logType } );

        WHEN( "Serializing and deserializing configuration" )
        {
            const auto serialized = manager.serializeConfiguration();
            REQUIRE( manager.deserializeConfiguration( serialized ).success );

            THEN( "Configuration is restored correctly" )
            {
                auto logTypes = manager.getLogTypes();
                REQUIRE( logTypes.size() == 1 );
                REQUIRE( logTypes[0].name == "Test Log" );
            }
        }
    }
}

SCENARIO( "Complete event search and export workflow", "[event][workflow]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    auto convertToNonCapturing = []( const QString& pattern ) -> QString {
        static QRegularExpression namedGroupRx( R"(\(\?<([^>]+)>)" );
        QString result = pattern;
        result.replace( namedGroupRx, "(?:" );
        return result;
    };

    GIVEN( "Event rules with named capture groups" )
    {
        manager.setEventRules( {} );

        EventRule httpRule;
        httpRule.name = "HTTP Request";
        httpRule.category = "Network";
        httpRule.enabled = true;
        httpRule.eventType = EventType::Instance;
        httpRule.pattern = R"(HTTP request.*method=(?<method>\w+).*url=(?<url>[\w/]+))";
        httpRule.parameterNames = { "method", "url" };
        manager.addEventRule( httpRule );

        EventRule dbRule;
        dbRule.name = "DB Query";
        dbRule.category = "Database";
        dbRule.enabled = true;
        dbRule.eventType = EventType::Instance;
        dbRule.pattern = R"(Query executed.*table=(?<table>\w+).*rows=(?<rows>\d+))";
        dbRule.parameterNames = { "table", "rows" };
        manager.addEventRule( dbRule );

        WHEN( "Building combined search pattern" )
        {
            auto allRules = manager.getEventRules();
            QStringList patterns;

            for ( const auto& rule : allRules ) {
                if ( !rule.enabled ) continue;
                if ( rule.eventType == EventType::Instance && !rule.pattern.empty() ) {
                    patterns.append( QString( "(?:%1)" ).arg(
                        convertToNonCapturing( QString::fromStdString( rule.pattern ) ) ) );
                }
            }

            QString combinedPattern = patterns.join( "|" );
            QRegularExpression searchRegex( combinedPattern, QRegularExpression::CaseInsensitiveOption );

            THEN( "Combined pattern is valid" )
            {
                REQUIRE( searchRegex.isValid() );
            }

            AND_WHEN( "Searching and extracting parameters" )
            {
                QStringList logLines = {
                    "2026-03-12T10:00:00.000 INFO HTTP request method=GET url=/api/users",
                    "2026-03-12T10:00:01.000 DEBUG Some other log line",
                    "2026-03-12T10:00:02.000 INFO Query executed table=users rows=42",
                    "2026-03-12T10:00:03.000 INFO HTTP request method=POST url=/api/orders",
                };

                QVector<int> matchingIndices;
                for ( int i = 0; i < logLines.size(); ++i ) {
                    if ( searchRegex.match( logLines[i] ).hasMatch() ) {
                        matchingIndices.append( i );
                    }
                }

                QVector<QMap<QString, QString>> extractedParams;
                for ( int idx : matchingIndices ) {
                    const QString& line = logLines[idx];

                    for ( const auto& rule : allRules ) {
                        if ( !rule.enabled ) continue;

                        QRegularExpression originalRegex( QString::fromStdString( rule.pattern ),
                                                         QRegularExpression::CaseInsensitiveOption );
                        QRegularExpressionMatch match = originalRegex.match( line );

                        if ( match.hasMatch() ) {
                            QMap<QString, QString> params;
                            for ( const auto& param : rule.parameterNames ) {
                                const QString paramName = QString::fromStdString( param );
                                params[paramName] = match.captured( paramName );
                            }
                            extractedParams.append( params );
                            break;
                        }
                    }
                }

                THEN( "Parameters are correctly extracted" )
                {
                    REQUIRE( matchingIndices.size() == 3 );
                    REQUIRE( extractedParams.size() == 3 );

                    REQUIRE( extractedParams[0]["method"] == "GET" );
                    REQUIRE( extractedParams[0]["url"] == "/api/users" );

                    REQUIRE( extractedParams[1]["table"] == "users" );
                    REQUIRE( extractedParams[1]["rows"] == "42" );

                    REQUIRE( extractedParams[2]["method"] == "POST" );
                    REQUIRE( extractedParams[2]["url"] == "/api/orders" );
                }
            }
        }
    }
}

SCENARIO( "Event rule mapping with category filter", "[event]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    GIVEN( "Event rules with different categories and some disabled" )
    {
        std::vector<EventRule> rules;

        EventRule networkRule;
        networkRule.name = "http_request";
        networkRule.category = "Network";
        networkRule.eventType = EventType::Instance;
        networkRule.pattern = "HTTP request.*url=(?<url>\\S+)";
        networkRule.enabled = false;
        rules.push_back( networkRule );

        EventRule indexingRule1;
        indexingRule1.name = "file_index";
        indexingRule1.category = "Indexing";
        indexingRule1.eventType = EventType::Instance;
        indexingRule1.pattern = "Indexing file (?<file>\\S+)";
        indexingRule1.enabled = true;
        rules.push_back( indexingRule1 );

        EventRule dbRule;
        dbRule.name = "db_query";
        dbRule.category = "Database";
        dbRule.eventType = EventType::Instance;
        dbRule.pattern = "Query.*table=(?<table>\\S+)";
        dbRule.enabled = true;
        rules.push_back( dbRule );

        EventRule indexingRule2;
        indexingRule2.name = "file_scan";
        indexingRule2.category = "Indexing";
        indexingRule2.eventType = EventType::Instance;
        indexingRule2.pattern = "Scanning dir (?<dir>\\S+)";
        indexingRule2.enabled = false;
        rules.push_back( indexingRule2 );

        manager.setEventRules( rules );

        struct CompiledRule {
            QString ruleName;
            QRegularExpression patternRegex;
        };

        WHEN( "Filtering by 'Indexing' category" )
        {
            QString category = "Indexing";
            QVector<CompiledRule> compiledRules;
            QStringList patterns;

            for ( const auto& rule : rules ) {
                if ( !rule.enabled ) continue;
                if ( !category.isEmpty() && QString::fromStdString( rule.category ) != category )
                    continue;

                CompiledRule cr;
                cr.ruleName = QString::fromStdString( rule.name );
                cr.patternRegex = QRegularExpression( QString::fromStdString( rule.pattern ),
                                                      QRegularExpression::CaseInsensitiveOption );
                compiledRules.append( cr );

                QString nonCapturing = QString::fromStdString( rule.pattern );
                nonCapturing.replace( QRegularExpression( R"(\(\?<([^>]+)>)" ), "(?:" );
                patterns.append( QString( "(?:%1)" ).arg( nonCapturing ) );
            }

            THEN( "Only 1 rule should be compiled (file_index)" )
            {
                REQUIRE( compiledRules.size() == 1 );
                REQUIRE( compiledRules[0].ruleName == "file_index" );
            }

            AND_WHEN( "Mapping matched lines to rules" )
            {
                QString testLine = "Indexing file test.log";

                int matchedRuleIndex = -1;
                for ( int i = 0; i < compiledRules.size(); ++i ) {
                    if ( compiledRules[i].patternRegex.match( testLine ).hasMatch() ) {
                        matchedRuleIndex = i;
                        break;
                    }
                }

                THEN( "Matched index should be 0 (compiledRules index)" )
                {
                    REQUIRE( matchedRuleIndex == 0 );
                }

                AND_THEN( "Rule name should come from compiledRules, not original rules array" )
                {
                    REQUIRE( compiledRules[matchedRuleIndex].ruleName == "file_index" );
                }
            }
        }

        WHEN( "Filtering by 'All Categories' (empty string)" )
        {
            QString category;
            QVector<CompiledRule> compiledRules;

            for ( const auto& rule : rules ) {
                if ( !rule.enabled ) continue;
                if ( !category.isEmpty() && QString::fromStdString( rule.category ) != category )
                    continue;

                CompiledRule cr;
                cr.ruleName = QString::fromStdString( rule.name );
                cr.patternRegex = QRegularExpression( QString::fromStdString( rule.pattern ),
                                                      QRegularExpression::CaseInsensitiveOption );
                compiledRules.append( cr );
            }

            THEN( "2 rules should be compiled (file_index and db_query)" )
            {
                REQUIRE( compiledRules.size() == 2 );
                REQUIRE( compiledRules[0].ruleName == "file_index" );
                REQUIRE( compiledRules[1].ruleName == "db_query" );
            }

            AND_WHEN( "Testing index mapping for db_query rule" )
            {
                QString testLine = "Query executed table=users";

                int matchedRuleIndex = -1;
                for ( int i = 0; i < compiledRules.size(); ++i ) {
                    if ( compiledRules[i].patternRegex.match( testLine ).hasMatch() ) {
                        matchedRuleIndex = i;
                        break;
                    }
                }

                THEN( "Matched index should be 1" )
                {
                    REQUIRE( matchedRuleIndex == 1 );
                }

                AND_THEN( "Rule name should be db_query from compiledRules" )
                {
                    REQUIRE( compiledRules[matchedRuleIndex].ruleName == "db_query" );
                }
            }
        }
    }

    manager.setEventRules( {} );
}

SCENARIO( "Event filtering with multiple categories", "[event]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    GIVEN( "Event rules in different categories" )
    {
        manager.setEventRules( {} );

        EventRule networkRule;
        networkRule.name = "http_request";
        networkRule.category = "Network";
        networkRule.eventType = EventType::Instance;
        networkRule.pattern = "HTTP request";
        networkRule.enabled = true;
        manager.addEventRule( networkRule );

        EventRule indexRule;
        indexRule.name = "file_index";
        indexRule.category = "Indexing";
        indexRule.eventType = EventType::Instance;
        indexRule.pattern = "Indexing file";
        indexRule.enabled = true;
        manager.addEventRule( indexRule );

        EventRule dbRule;
        dbRule.name = "db_query";
        dbRule.category = "Database";
        dbRule.eventType = EventType::Instance;
        dbRule.pattern = "Query executed";
        dbRule.enabled = true;
        manager.addEventRule( dbRule );

        EventRule disabledRule;
        disabledRule.name = "disabled_event";
        disabledRule.category = "Network";
        disabledRule.eventType = EventType::Instance;
        disabledRule.pattern = "Disabled pattern";
        disabledRule.enabled = false;
        manager.addEventRule( disabledRule );

        auto allRules = manager.getEventRules();
        REQUIRE( allRules.size() == 4 );

        WHEN( "Filtering with empty category list (All Categories)" )
        {
            QStringList selectedCategories;

            std::vector<EventRule> filteredRules;
            for ( const auto& rule : allRules ) {
                if ( !rule.enabled ) continue;
                if ( !selectedCategories.isEmpty()
                     && !selectedCategories.contains( QString::fromStdString( rule.category ) ) )
                    continue;
                filteredRules.push_back( rule );
            }

            THEN( "All enabled rules are included" )
            {
                REQUIRE( filteredRules.size() == 3 );
            }
        }

        WHEN( "Filtering with single category" )
        {
            QStringList selectedCategories = { "Network" };

            std::vector<EventRule> filteredRules;
            for ( const auto& rule : allRules ) {
                if ( !rule.enabled ) continue;
                if ( !selectedCategories.isEmpty()
                     && !selectedCategories.contains( QString::fromStdString( rule.category ) ) )
                    continue;
                filteredRules.push_back( rule );
            }

            THEN( "Only rules from Network category are included" )
            {
                REQUIRE( filteredRules.size() == 1 );
                REQUIRE( filteredRules[0].name == "http_request" );
            }
        }

        WHEN( "Filtering with multiple categories" )
        {
            QStringList selectedCategories = { "Network", "Indexing" };

            std::vector<EventRule> filteredRules;
            for ( const auto& rule : allRules ) {
                if ( !rule.enabled ) continue;
                if ( !selectedCategories.isEmpty()
                     && !selectedCategories.contains( QString::fromStdString( rule.category ) ) )
                    continue;
                filteredRules.push_back( rule );
            }

            THEN( "Rules from both categories are included" )
            {
                REQUIRE( filteredRules.size() == 2 );

                QStringList names;
                for ( const auto& rule : filteredRules ) {
                    names.append( QString::fromStdString( rule.name ) );
                }
                REQUIRE( names.contains( "http_request" ) );
                REQUIRE( names.contains( "file_index" ) );
                REQUIRE( !names.contains( "db_query" ) );
            }
        }

        WHEN( "Filtering with all three categories" )
        {
            QStringList selectedCategories = { "Network", "Indexing", "Database" };

            std::vector<EventRule> filteredRules;
            for ( const auto& rule : allRules ) {
                if ( !rule.enabled ) continue;
                if ( !selectedCategories.isEmpty()
                     && !selectedCategories.contains( QString::fromStdString( rule.category ) ) )
                    continue;
                filteredRules.push_back( rule );
            }

            THEN( "All enabled rules from all three categories are included" )
            {
                REQUIRE( filteredRules.size() == 3 );
            }
        }

        WHEN( "Filtering with non-existent category" )
        {
            QStringList selectedCategories = { "NonExistent" };

            std::vector<EventRule> filteredRules;
            for ( const auto& rule : allRules ) {
                if ( !rule.enabled ) continue;
                if ( !selectedCategories.isEmpty()
                     && !selectedCategories.contains( QString::fromStdString( rule.category ) ) )
                    continue;
                filteredRules.push_back( rule );
            }

            THEN( "No rules match" )
            {
                REQUIRE( filteredRules.empty() );
            }
        }
    }

    manager.setEventRules( {} );
}

SCENARIO( "Multi-category export produces correct Perfetto output", "[event]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    GIVEN( "Events from multiple categories" )
    {
        LogEvent networkEvent;
        networkEvent.name = "http_request";
        networkEvent.category = "Network";
        networkEvent.timestamp = 1000000000;
        networkEvent.pid = 1;
        networkEvent.tid = 100;
        networkEvent.eventType = EventType::Instance;
        networkEvent.hasValidTimestamp = true;

        LogEvent indexEvent;
        indexEvent.name = "file_index";
        indexEvent.category = "Indexing";
        indexEvent.timestamp = 2000000000;
        indexEvent.pid = 1;
        indexEvent.tid = 100;
        indexEvent.eventType = EventType::Instance;
        indexEvent.hasValidTimestamp = true;

        LogEvent dbEvent;
        dbEvent.name = "db_query";
        dbEvent.category = "Database";
        dbEvent.timestamp = 3000000000;
        dbEvent.pid = 1;
        dbEvent.tid = 100;
        dbEvent.eventType = EventType::Instance;
        dbEvent.hasValidTimestamp = true;

        std::vector<LogEvent> allEvents = { networkEvent, indexEvent, dbEvent };

        WHEN( "Exporting all events" )
        {
            auto perfettoEvents = manager.convertToPerfettoFormat( allEvents );

            THEN( "All events are in output" )
            {
                REQUIRE( perfettoEvents.size() == 3 );
            }
        }

        WHEN( "Filtering and exporting selected categories" )
        {
            QStringList selectedCategories = { "Network", "Indexing" };

            std::vector<LogEvent> filteredEvents;
            for ( const auto& event : allEvents ) {
                if ( selectedCategories.contains( QString::fromStdString( event.category ) ) ) {
                    filteredEvents.push_back( event );
                }
            }

            auto perfettoEvents = manager.convertToPerfettoFormat( filteredEvents );

            THEN( "Only selected category events are exported" )
            {
                REQUIRE( perfettoEvents.size() == 2 );

                QStringList categories;
                for ( const auto& pe : perfettoEvents ) {
                    categories.append( QString::fromStdString( pe.cat ) );
                }
                REQUIRE( categories.contains( "Network" ) );
                REQUIRE( categories.contains( "Indexing" ) );
                REQUIRE( !categories.contains( "Database" ) );
            }
        }
    }
}

SCENARIO( "parseLine timestamps use mktime with local timezone", "[event][regression]" )
{
    EventVisualizationManagerResetGuard reset;
    EventVisualizationManager& manager = EventVisualizationManager::getInstance();

    LogParsingRule parsingRule;
    parsingRule.name = "ISO";
    parsingRule.parsePattern = "^(?<timestamp>\\S+)\\s+(?<message>.*)$";
    parsingRule.timestampFormat = "yyyy-MM-ddTHH:mm:ss.zzz";
    parsingRule.enabled = true;

    EventRule eventRule;
    eventRule.name = "any";
    eventRule.pattern = ".*";
    eventRule.enabled = true;

    const auto expectedNs = []( int year, int month, int day, int hour, int min, int sec,
                                int millis ) -> int64_t {
        std::tm tm{};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = min;
        tm.tm_sec = sec;
        tm.tm_isdst = -1;
        const std::time_t epoch = std::mktime( &tm );
        REQUIRE( epoch != static_cast<std::time_t>( -1 ) );
        return static_cast<int64_t>( epoch ) * 1000000000LL
               + static_cast<int64_t>( millis ) * 1000000LL;
    };

    // Winter and summer rows pin DST handling; the expected value is computed
    // with the same local mktime the implementation must use.
    const std::vector<std::pair<std::string, int64_t>> cases = {
        { "2026-01-15T08:30:45.123", expectedNs( 2026, 1, 15, 8, 30, 45, 123 ) },
        { "2026-03-10T10:30:45.123", expectedNs( 2026, 3, 10, 10, 30, 45, 123 ) },
        { "2026-07-15T23:59:59.999", expectedNs( 2026, 7, 15, 23, 59, 59, 999 ) },
        { "2026-12-31T00:00:00.000", expectedNs( 2026, 12, 31, 0, 0, 0, 0 ) },
    };

    for ( const auto& [ line, expected ] : cases ) {
        LogEvent event = manager.parseLine( line + " message", eventRule, 0_lnum, &parsingRule );
        REQUIRE( event.hasValidTimestamp );
        REQUIRE( event.timestamp == expected );
    }
}
