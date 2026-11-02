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

#ifndef KLOGG_HIGHLIGHTERSSTORE_H
#define KLOGG_HIGHLIGHTERSSTORE_H

#include "highlightersetcollection.h"

namespace platform {
class Settings;
}

namespace klogg::ui {

// Reads/writes the highlight sets and the quick highlighter palette from/to a
// platform settings document. The groups and keys match the historical core
// format (HighlighterSetCollection / HighlighterSet / Highlighter), so existing
// configurations load unchanged and no migration is needed.
void loadHighlighters( platform::Settings& settings, HighlighterSetCollection& collection );
void saveHighlighters( platform::Settings& settings, const HighlighterSetCollection& collection );

} // namespace klogg::ui

#endif // KLOGG_HIGHLIGHTERSSTORE_H
