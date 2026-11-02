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

#include "apievents.h"

#include <utility>

#include "event_mapping.h"
#include "eventvisualization.h"
#include "log.h"
#include "log_qt.h"
#include "logtypesstore.h"

namespace klogg::ui {

std::vector<klogg::api::LogTypeConfig> listLogTypeConfigs()
{
    return LogTypesStore::instance().configs();
}

bool replaceLogTypeConfigs( std::vector<klogg::api::LogTypeConfig> configs )
{
    LogTypesStore::instance().replaceConfigs( std::move( configs ) );
    return true;
}

std::optional<klogg::api::LogTypeConfig> matchLogTypeForFile( const std::string& filePath )
{
    return LogTypesStore::instance().matchForFile( filePath );
}

QVector<EventRule> allEventRules()
{
    return allEventRules( listLogTypeConfigs() );
}

QVector<EventRule> allEventRules( const std::vector<klogg::api::LogTypeConfig>& configs )
{
    QVector<EventRule> rules;
    for ( const auto& config : configs ) {
        for ( const auto& [ category, categoryRules ] : config.eventRulesByCategory ) {
            for ( const auto& rule : categoryRules ) {
                rules.append( klogg::api::nativeEventRuleFromApi( rule ) );
            }
        }
    }
    return rules;
}

bool hasAnyParsingRule( const std::vector<klogg::api::LogTypeConfig>& configs )
{
    for ( const auto& config : configs ) {
        if ( config.enabled && !config.parsingRule.parsePattern.empty() ) {
            return true;
        }
    }
    return false;
}

bool addEventRule( const EventRule& rule )
{
    auto configs = listLogTypeConfigs();

    // Legacy placement: first config, creating a default one when none exists
    if ( configs.empty() ) {
        klogg::api::LogTypeConfig config{};
        config.name = "Default";
        config.globPattern = "*";
        config.isDefault = true;
        const auto category = rule.category.empty() ? std::string{ "General" } : rule.category;
        config.eventRulesByCategory[ category ].push_back(
            klogg::api::apiEventRuleFromNative( rule ) );
        configs.push_back( std::move( config ) );
        return replaceLogTypeConfigs( std::move( configs ) );
    }

    auto& config = configs.front();
    const auto category = rule.category.empty() ? std::string{ "General" } : rule.category;
    auto& categoryRules = config.eventRulesByCategory[ category ];
    for ( auto& existing : categoryRules ) {
        if ( existing.name == rule.name ) {
            existing = klogg::api::apiEventRuleFromNative( rule );
            return replaceLogTypeConfigs( std::move( configs ) );
        }
    }
    categoryRules.push_back( klogg::api::apiEventRuleFromNative( rule ) );
    return replaceLogTypeConfigs( std::move( configs ) );
}

bool updateEventRule( const QString& name, const EventRule& rule )
{
    auto configs = listLogTypeConfigs();
    const auto key = name.toStdString();
    for ( auto& config : configs ) {
        for ( auto& [ category, categoryRules ] : config.eventRulesByCategory ) {
            for ( auto& existing : categoryRules ) {
                if ( existing.name == key ) {
                    existing = klogg::api::apiEventRuleFromNative( rule );
                    return replaceLogTypeConfigs( std::move( configs ) );
                }
            }
        }
    }
    // Unknown name falls back to an add, same as the legacy manager
    return addEventRule( rule );
}

bool removeEventRule( const QString& name )
{
    auto configs = listLogTypeConfigs();
    const auto key = name.toStdString();
    for ( auto& config : configs ) {
        for ( auto& [ category, categoryRules ] : config.eventRulesByCategory ) {
            const auto it = std::find_if( categoryRules.begin(), categoryRules.end(),
                                          [ & ]( const auto& rule ) { return rule.name == key; } );
            if ( it != categoryRules.end() ) {
                categoryRules.erase( it );
                return replaceLogTypeConfigs( std::move( configs ) );
            }
        }
    }
    return false;
}

} // namespace klogg::ui
