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

#include "highlightersstore.h"

#include <algorithm>

#include "log.h"
#include "settings.h"
#include "storage_utils.h"

namespace klogg::ui {
namespace {

using klogg::state::loadRgba;
using klogg::state::saveRgba;

constexpr int kHighlighterSetVersion = 3;
constexpr int kCollectionVersion = 2;

Highlighter loadHighlighter( platform::Settings& settings )
{
    Highlighter highlighter;
    highlighter.setPattern( settings.getString( "regexp" ) );
    highlighter.setIgnoreCase( settings.getBool( "ignore_case", false ) );
    highlighter.setHighlightOnlyMatch( settings.getBool( "match_only", false ) );
    highlighter.setUseRegex( settings.getBool( "use_regex", true ) );
    highlighter.setVariateColors( settings.getBool( "variate_colors", false ) );
    highlighter.setColorVariance( settings.getInt( "color_variance", 15 ) );
    highlighter.setForeColor( loadRgba( settings, "fore_colour" ) );
    highlighter.setBackColor( loadRgba( settings, "back_colour" ) );
    const auto useThemeBack = settings.getBool( "use_theme_back", true );
    highlighter.setUseThemeBack( useThemeBack );
    if ( useThemeBack ) {
        // Matches the historical reader: a theme background means there is no
        // explicit back color
        highlighter.setBackColor( klogg::Rgba{} );
    }
    highlighter.setName( settings.getString( "name" ) );
    highlighter.setEnabled( settings.getBool( "enabled", true ) );
    return highlighter;
}

void saveHighlighter( platform::Settings& settings, const Highlighter& highlighter )
{
    settings.setString( "regexp", highlighter.pattern() );
    settings.setBool( "ignore_case", highlighter.ignoreCase() );
    settings.setBool( "match_only", highlighter.highlightOnlyMatch() );
    settings.setBool( "use_regex", highlighter.useRegex() );
    settings.setBool( "variate_colors", highlighter.variateColors() );
    settings.setInt( "color_variance", highlighter.colorVariance() );
    saveRgba( settings, "fore_colour", highlighter.foreColor() );
    saveRgba( settings, "back_colour", highlighter.backColor() );
    settings.setBool( "use_theme_back", highlighter.useThemeBack() );
    // Optional keys: absent means the default (empty name, enabled) for older
    // readers
    if ( !highlighter.name().empty() ) {
        settings.setString( "name", highlighter.name() );
    }
    else {
        settings.remove( "name" );
    }
    if ( !highlighter.isEnabled() ) {
        settings.setBool( "enabled", highlighter.isEnabled() );
    }
    else {
        settings.remove( "enabled" );
    }
}

HighlighterSet loadHighlighterSet( platform::Settings& settings )
{
    settings.beginGroup( "HighlighterSet" );
    auto set = HighlighterSet::createNewSet( settings.getString( "name", "Highlighters set" ) );
    const auto id = settings.getString( "id" );
    if ( !id.empty() ) {
        set.setId( id );
    }

    klogg::vector<Highlighter> highlighters;
    const auto size = settings.beginReadArray( "highlighters" );
    highlighters.reserve( static_cast<size_t>( size ) );
    for ( int i = 0; i < size; ++i ) {
        settings.setArrayIndex( i );
        highlighters.push_back( loadHighlighter( settings ) );
    }
    settings.endArray();
    set.setHighlighters( highlighters );
    settings.endGroup();
    return set;
}

void saveHighlighterSet( platform::Settings& settings, const HighlighterSet& set )
{
    settings.beginGroup( "HighlighterSet" );
    settings.setInt( "version", kHighlighterSetVersion );
    settings.setString( "name", set.name() );
    settings.setString( "id", set.id() );
    settings.remove( "highlighters" );
    settings.beginWriteArray( "highlighters" );
    const auto& highlighters = set.highlighters();
    for ( size_t i = 0; i < highlighters.size(); ++i ) {
        settings.setArrayIndex( static_cast<int>( i ) );
        saveHighlighter( settings, highlighters[ i ] );
    }
    settings.endArray();
    settings.endGroup();
}

} // namespace

void loadHighlighters( platform::Settings& settings, HighlighterSetCollection& collection )
{
    klogg::vector<HighlighterSet> sets;
    klogg::vector<QuickHighlighter> quickHighlighters;
    std::vector<std::string> activeSetIds;

    if ( settings.contains( "HighlighterSetCollection/version" ) ) {
        settings.beginGroup( "HighlighterSetCollection" );
        if ( settings.getInt( "version", 0 ) <= kCollectionVersion ) {
            const auto size = settings.beginReadArray( "sets" );
            sets.reserve( static_cast<size_t>( size ) );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( i );
                sets.push_back( loadHighlighterSet( settings ) );
            }
            settings.endArray();

            activeSetIds = settings.getStringList( "active_sets" );

            const auto quickCount = settings.beginReadArray( "quick" );
            quickHighlighters.reserve( static_cast<size_t>( quickCount ) );
            for ( int i = 0; i < quickCount; ++i ) {
                settings.setArrayIndex( i );
                QuickHighlighter quickHighlighter;
                quickHighlighter.color.foreColor = loadRgba( settings, "fore_colour" );
                quickHighlighter.color.backColor = loadRgba( settings, "back_colour" );
                quickHighlighter.useInCycle = settings.getBool( "cycle", true );
                quickHighlighter.name
                    = settings.getString( "name", "Color label " + std::to_string( i + 1 ) );
                quickHighlighters.push_back( std::move( quickHighlighter ) );
            }
            settings.endArray();
        }
        else {
            LOG_ERROR << "Unknown version of HighlighterSetCollection, ignoring it...";
        }
        settings.endGroup();
    }

    collection.setHighlighterSets( sets );
    for ( const auto& id : activeSetIds ) {
        collection.activateSet( id );
    }
    collection.setQuickHighlighters( quickHighlighters );
}

void saveHighlighters( platform::Settings& settings, const HighlighterSetCollection& collection )
{
    settings.beginGroup( "HighlighterSetCollection" );
    settings.setInt( "version", kCollectionVersion );
    settings.setStringList( "active_sets", collection.activeSetIds() );

    settings.remove( "sets" );
    settings.beginWriteArray( "sets" );
    const auto sets = collection.highlighterSets();
    for ( size_t i = 0; i < sets.size(); ++i ) {
        settings.setArrayIndex( static_cast<int>( i ) );
        saveHighlighterSet( settings, sets[ i ] );
    }
    settings.endArray();

    settings.remove( "quick" );
    settings.beginWriteArray( "quick" );
    const auto quickHighlighters = collection.quickHighlighters();
    for ( size_t i = 0; i < quickHighlighters.size(); ++i ) {
        settings.setArrayIndex( static_cast<int>( i ) );
        settings.setString( "name", quickHighlighters[ i ].name );
        saveRgba( settings, "fore_colour", quickHighlighters[ i ].color.foreColor );
        saveRgba( settings, "back_colour", quickHighlighters[ i ].color.backColor );
        settings.setBool( "cycle", quickHighlighters[ i ].useInCycle );
    }
    settings.endArray();
    settings.endGroup();
}

} // namespace klogg::ui
