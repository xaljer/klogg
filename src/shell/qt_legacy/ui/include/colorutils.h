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

#ifndef KLOGG_UI_COLORUTILS_H
#define KLOGG_UI_COLORUTILS_H

#include <QColor>

#include "color.h"

// Qt boundary conversions for the Qt-free klogg::Rgba type.
namespace klogg::ui {

inline QColor qcolorFromRgba( const klogg::Rgba& color )
{
    if ( color.a == 0 ) {
        return {};
    }
    return QColor( color.r, color.g, color.b, color.a );
}

inline klogg::Rgba rgbaFromQColor( const QColor& color )
{
    if ( !color.isValid() ) {
        return {};
    }
    return klogg::Rgba( static_cast<uint8_t>( color.red() ),
                        static_cast<uint8_t>( color.green() ),
                        static_cast<uint8_t>( color.blue() ),
                        static_cast<uint8_t>( color.alpha() ) );
}

} // namespace klogg::ui

#endif // KLOGG_UI_COLORUTILS_H
