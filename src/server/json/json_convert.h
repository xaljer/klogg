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

#include <string>

#include <nlohmann/json.hpp>

#include "klogg/api.h"

namespace klogg::server {

// Conversion between the Core API value types and the JSON shapes of
// docs/architecture/web-api.md. Field names are camelCase there.
nlohmann::json toJson( const api::FileInfo& info );
nlohmann::json toJson( const api::LineData& line );
nlohmann::json toJson( const api::SearchResult& result );
nlohmann::json toJson( const api::PatternInfo& info );
nlohmann::json toJson( const api::ViewportResult& result );
nlohmann::json toJson( const api::RenderedHtml& result );

const char* errorCodeName( api::ErrorCode code );
int httpStatus( api::ErrorCode code );

// Request parsing. Malformed values throw std::invalid_argument or a
// nlohmann::json exception; the dispatcher turns both into a 400.
api::AnsiMode parseAnsiMode( const nlohmann::json& params );
api::SearchParams parseSearchParams( const nlohmann::json& params );
api::ViewportOptions parseViewportOptions( const nlohmann::json& params );
api::HtmlRenderOptions parseHtmlRenderOptions( const nlohmann::json& params );

} // namespace klogg::server
