/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

#include <catch2/catch.hpp>

#include <QDir>
#include <QFile>
#include <QUuid>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "klogg/api_events.h"
#include "logtypesstore.h"

namespace {

std::filesystem::path tempJsonPath()
{
    return std::filesystem::path(
        ( QDir::tempPath() + "/klogg_logtypes_store_"
          + QUuid::createUuid().toString( QUuid::WithoutBraces ) + ".json" )
            .toStdString() );
}

} // namespace

TEST_CASE( "log types storage round trip", "[ui][logtypesstore]" )
{
    const auto file = tempJsonPath();

    std::vector<klogg::api::LogTypeConfig> configs;
    {
        klogg::api::LogTypeConfig config;
        config.name = "Java Application Log";
        config.globPattern = "*.java.log";
        config.parsingRule.name = "java";
        config.parsingRule.parsePattern = "(?<timestamp>\\S+)";
        config.parsingRule.timestampFormat = "%Y-%m-%d";
        config.enabled = false;
        config.isDefault = true;

        klogg::api::EventRule rule;
        rule.name = "db connect";
        rule.category = "database";
        rule.pattern = "DB connect";
        rule.eventType = klogg::api::EventType::Instance;
        config.eventRulesByCategory[ "database" ].push_back( rule );

        configs.push_back( config );
    }

    REQUIRE( klogg::ui::saveLogTypesToFile( file, configs ) );
    REQUIRE( std::filesystem::exists( file ) );

    bool loadFailed = true;
    const auto loaded = klogg::ui::loadLogTypesFromFile( file, &loadFailed );

    REQUIRE_FALSE( loadFailed );
    REQUIRE( loaded.size() == 1 );
    const auto& config = loaded[ 0 ];
    REQUIRE( config.name == "Java Application Log" );
    REQUIRE( config.globPattern == "*.java.log" );
    REQUIRE_FALSE( config.enabled );
    REQUIRE( config.isDefault );
    REQUIRE( config.parsingRule.parsePattern == "(?<timestamp>\\S+)" );
    REQUIRE( config.parsingRule.timestampFormat == "%Y-%m-%d" );
    REQUIRE( config.eventRulesByCategory.count( "database" ) == 1 );
    REQUIRE( config.eventRulesByCategory.at( "database" ).size() == 1 );
    REQUIRE( config.eventRulesByCategory.at( "database" )[ 0 ].pattern == "DB connect" );

    QFile::remove( QString::fromStdString( file.string() ) );
}

TEST_CASE( "log types storage missing file loads empty without failure", "[ui][logtypesstore]" )
{
    const auto file = tempJsonPath();
    bool loadFailed = true;
    const auto loaded = klogg::ui::loadLogTypesFromFile( file, &loadFailed );
    REQUIRE_FALSE( loadFailed );
    REQUIRE( loaded.empty() );
}

TEST_CASE( "log types storage damaged file reports failure", "[ui][logtypesstore]" )
{
    const auto file = tempJsonPath();
    {
        std::ofstream output( file, std::ios::binary | std::ios::trunc );
        output << "{ this is not valid json";
    }

    bool loadFailed = false;
    const auto loaded = klogg::ui::loadLogTypesFromFile( file, &loadFailed );
    REQUIRE( loadFailed );
    REQUIRE( loaded.empty() );

    QFile::remove( QString::fromStdString( file.string() ) );
}
