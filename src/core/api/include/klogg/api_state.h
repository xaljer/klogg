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
#include <vector>

#include "klogg/api_types.h"

// Types of the shared-state API: a consistent view of user state for all consumers.
namespace klogg::api {

struct HighlightRule {
    std::string name; // optional display name; empty = unnamed (fall back to the pattern)
    std::string pattern;
    bool useRegex = true;
    bool caseSensitive = true;
    std::string color; // foreground color, "#aarrggbb"
    // Background color, "#aarrggbb"; empty = no explicit background
    std::string backColor;
    // When true the background follows the theme and backColor stays empty
    bool useThemeBack = true;
    // Highlight the matched text only instead of the whole line
    bool onlyMatch = false;
    // Per-match color variation, effective only with onlyMatch
    bool variateColors = false;
    int colorVariance = 15;
    bool enabled = true;
};

// One color label slot of the quick highlighters palette. The palette is
// fixed-size: slots are edited in place, never added or removed. The words
// assigned to a slot are transient per-view state and stay outside the API.
struct QuickHighlighterDef {
    std::string name;
    std::string color;      // foreground color, "#aarrggbb"
    std::string backColor;  // background color, "#aarrggbb"
    bool useInCycle = true; // slot participates in the "next label" rotation
};

} // namespace klogg::api
