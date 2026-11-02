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

#ifndef KLOGG_RENDER_WRAP_H
#define KLOGG_RENDER_WRAP_H

#include <string_view>
#include <vector>

#include "linetypes.h"

namespace klogg::render {

// One wrapped segment of a logical line. Columns are UTF-16 code units of the
// line's display text - the unit the highlighters report match positions in -
// so every BMP code point (ASCII, CJK) is one column and a non-BMP code point
// (emoji) is two. Display width (double-width CJK) is not modelled here.
struct WrapPart {
    // 0-based index of this part inside the wrapped line.
    LineNumber line;
    // First column of the part in the unwrapped line.
    LineColumn start;
    // Number of columns the part covers.
    LineLength length;
};

// Splits the display text of one logical line into parts of at most wrapColumns
// columns - the same break points the paint path draws. The text is expected to
// be tab-expanded already, as the paint path untabifies before wrapping.
//
// The break rules:
// - a region is cut after its last space and that space ends the part;
// - a region without a space is cut hard at wrapColumns;
// - the remainder is emitted as-is once it fits, so a line that is exactly
//   wrapColumns wide yields one part and never a trailing empty one;
// - an empty line yields one empty part.
//
// A non-positive wrapColumns means "no wrapping": the loop would not terminate
// for such a width.
std::vector<WrapPart> wrapLine( std::string_view utf8Text, LineColumn wrapColumns );

} // namespace klogg::render

#endif // KLOGG_RENDER_WRAP_H
