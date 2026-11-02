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

#ifndef KLOGG_APIEVENTS_H
#define KLOGG_APIEVENTS_H

#include <optional>
#include <vector>

#include <QString>
#include <QVector>

#include "klogg/api_events.h"

struct EventRule;

// Bridge between the Qt UI and the Core API event visualization surface.
// The dialogs and views still speak the native types from
// src/state/eventvisualization.h; conversions use the shared mapping in
// src/state/include/event_mapping.h. Rule-level operations keep the legacy
// name-keyed semantics on top of the config-granular API (the first config
// holding the name wins).
namespace klogg::ui {

std::vector<klogg::api::LogTypeConfig> listLogTypeConfigs();
// Persists the whole list to the UI store and pushes it to the core session
bool replaceLogTypeConfigs( std::vector<klogg::api::LogTypeConfig> configs );
// Matches a config for a file path by its glob pattern; nullopt when none
std::optional<klogg::api::LogTypeConfig> matchLogTypeForFile( const std::string& filePath );

// All event rules of every config, in config/category order
QVector<EventRule> allEventRules();
QVector<EventRule> allEventRules( const std::vector<klogg::api::LogTypeConfig>& configs );
bool hasAnyParsingRule( const std::vector<klogg::api::LogTypeConfig>& configs );

bool addEventRule( const EventRule& rule );
bool updateEventRule( const QString& name, const EventRule& rule );
bool removeEventRule( const QString& name );

} // namespace klogg::ui

#endif // KLOGG_APIEVENTS_H
