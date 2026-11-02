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

#ifndef KLOGG_STORAGE_UTILS_H
#define KLOGG_STORAGE_UTILS_H

#include <string>

#include "color.h"
#include "settings.h"

namespace klogg::state {

// Colors are persisted as "#aarrggbb"; a default-constructed Rgba (alpha 0) is
// stored as an empty string, matching the historical on-disk format.
inline void saveRgba( platform::Settings& settings, const std::string& key,
                      const klogg::Rgba& color )
{
    settings.setString( key, color.a == 0 ? std::string{} : color.toHex() );
}

// Accepts the current "#aarrggbb" form and the legacy "#rrggbb" form; anything
// else (including named colours, which the old QColor reader accepted) yields
// the invalid sentinel.
inline klogg::Rgba loadRgba( platform::Settings& settings, const std::string& key )
{
    const auto value = settings.getString( key );
    if ( value.empty() ) {
        return {};
    }
    if ( const auto parsed = klogg::Rgba::fromHex( value ) ) {
        return *parsed;
    }
    if ( value.size() == 7 && value[ 0 ] == '#' ) {
        if ( const auto parsed = klogg::Rgba::fromHex( "#ff" + value.substr( 1 ) ) ) {
            return *parsed;
        }
    }
    return {};
}

} // namespace klogg::state

#endif // KLOGG_STORAGE_UTILS_H
