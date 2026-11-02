/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

#include <catch2/catch.hpp>

#include <QDir>
#include <QFile>
#include <QUuid>

#include <string>
#include <vector>

#include "highlighterset.h"
#include "highlightersetcollection.h"
#include "highlightersstore.h"
#include "settings.h"

namespace {

std::string tempSettingsPath()
{
    return ( QDir::tempPath() + "/klogg_highlighters_store_"
             + QUuid::createUuid().toString( QUuid::WithoutBraces ) + ".toml" )
        .toStdString();
}

} // namespace

TEST_CASE( "highlighters storage round trip", "[ui][highlightersstore]" )
{
    const auto path = tempSettingsPath();

    klogg::Rgba fore;
    klogg::Rgba back;
    std::string setId;
    {
        HighlighterSetCollection collection;
        auto set = HighlighterSet::createNewSet( "alpha" );
        setId = set.id();

        Highlighter rule;
        rule.setName( "errors" );
        rule.setPattern( "ERROR.*" );
        rule.setUseRegex( true );
        rule.setIgnoreCase( true );
        rule.setHighlightOnlyMatch( true );
        rule.setVariateColors( true );
        rule.setColorVariance( 42 );
        fore = *klogg::Rgba::fromHex( "#ff123456" );
        back = *klogg::Rgba::fromHex( "#ff654321" );
        rule.setForeColor( fore );
        rule.setUseThemeBack( false );
        rule.setBackColor( back );
        rule.setEnabled( false );

        klogg::vector<Highlighter> rules;
        rules.push_back( rule );
        set.setHighlighters( rules );

        klogg::vector<HighlighterSet> sets;
        sets.push_back( set );
        collection.setHighlighterSets( sets );
        collection.activateSet( setId );

        QuickHighlighter quick{ "custom label", { fore, back }, false };
        klogg::vector<QuickHighlighter> quicks;
        quicks.push_back( quick );
        collection.setQuickHighlighters( quicks );

        platform::Settings settings( path );
        klogg::ui::saveHighlighters( settings, collection );
        REQUIRE( settings.save() );
    }

    {
        platform::Settings settings( path );
        settings.load();

        HighlighterSetCollection loaded;
        klogg::ui::loadHighlighters( settings, loaded );

        const auto loadedSets = loaded.highlighterSets();
        REQUIRE( loadedSets.size() == 1 );
        const auto& set = loadedSets[ 0 ];
        REQUIRE( set.name() == "alpha" );
        REQUIRE( set.id() == setId );

        const auto active = loaded.activeSetIds();
        REQUIRE( active.size() == 1 );
        REQUIRE( active[ 0 ] == setId );

        const auto rules = set.highlighters();
        REQUIRE( rules.size() == 1 );
        const auto& rule = rules[ 0 ];
        REQUIRE( rule.name() == "errors" );
        REQUIRE( rule.pattern() == "ERROR.*" );
        REQUIRE( rule.useRegex() );
        REQUIRE( rule.ignoreCase() );
        REQUIRE( rule.highlightOnlyMatch() );
        REQUIRE( rule.variateColors() );
        REQUIRE( rule.colorVariance() == 42 );
        REQUIRE( rule.foreColor() == fore );
        REQUIRE( !rule.useThemeBack() );
        REQUIRE( rule.backColor() == back );
        REQUIRE( !rule.isEnabled() );

        REQUIRE( loaded.quickHighlighters().size() == 1 );
        REQUIRE( loaded.quickHighlighters()[ 0 ].name == "custom label" );
        REQUIRE( loaded.quickHighlighters()[ 0 ].color.foreColor == fore );
        REQUIRE( loaded.quickHighlighters()[ 0 ].color.backColor == back );
        REQUIRE( !loaded.quickHighlighters()[ 0 ].useInCycle );
    }

    QFile::remove( QString::fromStdString( path ) );
}

TEST_CASE( "highlighters storage legacy optional keys keep defaults", "[ui][highlightersstore]" )
{
    const auto path = tempSettingsPath();

    {
        // Hand-written old-format entry: none of the optional keys present
        platform::Settings settings( path );
        settings.beginGroup( "HighlighterSetCollection" );
        settings.setInt( "version", 2 );
        settings.setStringList( "active_sets", {} );
        settings.beginWriteArray( "sets" );
        settings.setArrayIndex( 0 );
        settings.beginGroup( "HighlighterSet" );
        settings.setInt( "version", 3 );
        settings.setString( "name", "legacy" );
        settings.setString( "id", "legacy-uuid" );
        settings.beginWriteArray( "highlighters" );
        settings.setArrayIndex( 0 );
        settings.setString( "regexp", "ERROR" );
        settings.setBool( "ignore_case", true );
        settings.setString( "fore_colour", "#ffff0000" );
        settings.endArray();
        settings.endGroup();
        settings.endArray();
        settings.endGroup();
        REQUIRE( settings.save() );
    }

    {
        platform::Settings settings( path );
        settings.load();

        HighlighterSetCollection loaded;
        klogg::ui::loadHighlighters( settings, loaded );

        const auto loadedSets = loaded.highlighterSets();
        REQUIRE( loadedSets.size() == 1 );
        const auto& set = loadedSets[ 0 ];
        REQUIRE( set.name() == "legacy" );
        REQUIRE( set.id() == "legacy-uuid" );
        REQUIRE( loaded.activeSetIds().empty() );

        const auto rules = set.highlighters();
        REQUIRE( rules.size() == 1 );
        const auto& rule = rules[ 0 ];
        REQUIRE( rule.name().empty() );
        REQUIRE( rule.pattern() == "ERROR" );
        REQUIRE( rule.useRegex() );
        REQUIRE( rule.ignoreCase() );
        REQUIRE( rule.foreColor() == *klogg::Rgba::fromHex( "#ffff0000" ) );
        REQUIRE( rule.backColor() == klogg::Rgba{} );
        REQUIRE( rule.useThemeBack() );
        REQUIRE( !rule.highlightOnlyMatch() );
        REQUIRE( !rule.variateColors() );
        REQUIRE( rule.colorVariance() == 15 );
        REQUIRE( rule.isEnabled() );
    }

    QFile::remove( QString::fromStdString( path ) );
}
