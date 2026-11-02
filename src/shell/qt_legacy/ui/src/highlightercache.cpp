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

#include "highlightercache.h"

#include <QColor>
#include <QString>

#include <vector>

#include "apisession.h"
#include "colorutils.h"
#include "highlightersstore.h"
#include "klogg/api_session.h"
#include "log.h"
#include "log_qt.h"
#include "settings.h"
#include "userdata.h"

namespace klogg::ui {
namespace {

std::string qColorToApiColor( const QColor& color )
{
    return color.isValid() ? color.name( QColor::HexArgb ).toStdString() : std::string{};
}

std::string rgbaToApiColor( const klogg::Rgba& color )
{
    return qColorToApiColor( klogg::ui::qcolorFromRgba( color ) );
}

klogg::api::HighlightRule highlightRuleToApi( const Highlighter& rule )
{
    klogg::api::HighlightRule apiRule;
    apiRule.name = rule.name();
    apiRule.pattern = rule.pattern();
    apiRule.useRegex = rule.useRegex();
    apiRule.caseSensitive = !rule.ignoreCase();
    apiRule.color = rgbaToApiColor( rule.foreColor() );
    apiRule.backColor = rgbaToApiColor( rule.backColor() );
    apiRule.useThemeBack = rule.useThemeBack();
    apiRule.onlyMatch = rule.highlightOnlyMatch();
    apiRule.variateColors = rule.variateColors();
    apiRule.colorVariance = rule.colorVariance();
    apiRule.enabled = rule.isEnabled();
    return apiRule;
}

klogg::api::QuickHighlighterDef quickHighlighterToApi( const QuickHighlighter& quickHighlighter )
{
    klogg::api::QuickHighlighterDef def;
    def.name = quickHighlighter.name;
    def.color = rgbaToApiColor( quickHighlighter.color.foreColor );
    def.backColor = rgbaToApiColor( quickHighlighter.color.backColor );
    def.useInCycle = quickHighlighter.useInCycle;
    return def;
}

// The core consumes a single flat rule list: the merged rules of the active
// sets, in collection order
std::vector<klogg::api::HighlightRule>
mergedRulesToApi( const HighlighterSetCollection& collection )
{
    const auto& activeRules = collection.currentActiveSet().highlighters();
    std::vector<klogg::api::HighlightRule> rules;
    rules.reserve( static_cast<size_t>( activeRules.size() ) );
    for ( const auto& rule : activeRules ) {
        rules.push_back( highlightRuleToApi( rule ) );
    }
    return rules;
}

} // namespace

HighlighterCache& HighlighterCache::instance()
{
    static HighlighterCache cache;
    return cache;
}

const HighlighterSet& HighlighterCache::activeSet()
{
    ensureLoaded();
    return collection_.currentActiveSet();
}

const klogg::vector<QuickHighlighter>& HighlighterCache::quickHighlighters()
{
    ensureLoaded();
    return collection_.quickHighlighters();
}

HighlighterSetCollection& HighlighterCache::collection()
{
    ensureLoaded();
    return collection_;
}

void HighlighterCache::ensureLoaded()
{
    if ( initialized_ ) {
        return;
    }
    initialized_ = true;
    loadHighlighters( platform::userdata::config(), collection_ );
    ensureDefaultQuickHighlighters();
}

void HighlighterCache::ensureDefaultQuickHighlighters()
{
    // The palette is fixed-size and historically seeded from the defaults; a
    // store that never held one (fresh install) keeps them
    if ( collection_.quickHighlighters().empty() ) {
        collection_.setQuickHighlighters( defaultQuickHighlighters() );
    }
}

void HighlighterCache::refresh()
{
    ensureLoaded();
    pushToSession();
}

void HighlighterCache::applyPersistedState()
{
    initialized_ = true;
    loadHighlighters( platform::userdata::config(), collection_ );
    ensureDefaultQuickHighlighters();
    pushToSession();
}

void HighlighterCache::pushToSession()
{
    // The dialog edits rules in place, so the merged set may be stale; the
    // activation mutators keep it current, this covers direct edits
    collection_.rebuildCombinedSet();

    auto& session = apiSession();

    const auto rules = mergedRulesToApi( collection_ );
    const auto setResult = session.setHighlighters( rules );
    if ( !setResult ) {
        LOG_ERROR << "HighlighterCache: failed to push highlight rules: "
                  << setResult.error().message;
    }

    std::vector<klogg::api::QuickHighlighterDef> defs;
    const auto& quickHighlighters = collection_.quickHighlighters();
    defs.reserve( static_cast<size_t>( quickHighlighters.size() ) );
    for ( const auto& quickHighlighter : quickHighlighters ) {
        defs.push_back( quickHighlighterToApi( quickHighlighter ) );
    }
    const auto quickResult = session.setQuickHighlighters( defs );
    if ( !quickResult ) {
        LOG_ERROR << "HighlighterCache: failed to push quick highlighters: "
                  << quickResult.error().message;
    }
}

void HighlighterCache::persist()
{
    auto& settings = platform::userdata::config();
    saveHighlighters( settings, collection_ );
    if ( !settings.save() ) {
        LOG_WARNING << "Failed to save highlighters to " << settings.path().string();
    }
}

} // namespace klogg::ui
