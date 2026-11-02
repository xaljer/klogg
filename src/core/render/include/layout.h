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

#ifndef KLOGG_RENDER_LAYOUT_H
#define KLOGG_RENDER_LAYOUT_H

#include <string_view>
#include <vector>

#include "color.h"
#include "highlightedmatch.h"
#include "linetypes.h"
#include "wrap.h"

namespace klogg::render {

// One drawable piece of a line: a run of the display text on a single visual
// row, painted with one pair of colours.
//
// The text is not copied in here: it is the display text sliced by
// [start, start + length) in UTF-16 code units, so the caller keeps its own
// string type and decodes the line once for all of its chunks. length is
// already clamped to the part of the text the run covers and is 0 for a run
// that exists only to move the row cursor (see startsRow), which paints
// nothing.
struct LineChunk {
    // First column of the run in the line's display text.
    LineColumn start;
    // Columns of text the run covers, clamped to the line.
    LineLength length;
    Rgba foreColor;
    Rgba backColor;
    // True when the run is painted at the start of a new visual row: the
    // caller advances the row before painting it. The first run of every
    // chunk keeps the row it starts in.
    bool startsRow = false;
};

struct LineLayoutInput {
    // Display text of the line: tabs expanded and ANSI codes stripped, in the
    // UTF-16 column space the highlighters report matches in. May be empty.
    std::string_view line;

    // Highlights of the line in UTF-16 columns of `line`. They may extend
    // outside the visible range and past the end of the line; they are clamped
    // to it. Moved in so the clamping does not copy them.
    HighlightedMatchRanges highlights;

    // Wrap width in columns. A non-positive value means no wrapping: the line
    // stays a single part and the window below is what applies.
    LineColumn wrapColumns = 0_lcol;

    // Window into the line, used when wrapping is off: the first visible
    // column and how many columns are visible from there.
    LineColumn firstColumn = 0_lcol;
    LineLength visibleColumns = 0_length;

    // Colours of the parts of the line no highlight covers.
    Rgba foreColor;
    Rgba backColor;
};

struct LineLayout {
    // Wrap parts of the line, always at least one; the count is the number of
    // visual rows the line occupies.
    std::vector<WrapPart> parts;
    // Runs to paint, in paint order. Together they cover the visible range of
    // the line; the text of a run is `line` sliced by its columns.
    std::vector<LineChunk> chunks;
};

// Lays out one line for painting: wraps it, clamps its highlights to the
// visible range and turns the highlighted ranges into the runs a painter can
// draw in order.
//
// Ported from the Qt paint path (abstractlogview.cpp drawTextArea and
// LineDrawer), keeping its semantics pointer for pointer:
// - the row of a run follows the wrap part it starts in, and every run after
//   the first one of a chunk moves to the next row (LineDrawer::draw:326-331);
// - a range starting exactly on a part boundary is preceded by an empty run,
//   which is what moves the cursor to the next row before any text is painted
//   (the range walk keeps a start sitting on a boundary in the previous part);
// - a highlight is clamped to [first visible, last visible] and skipped when
//   it clamps away to nothing (HighlightedMatchRanges::clamp);
// - a highlight without a back colour (alpha 0) keeps the caller's back
//   colour, and a highlight running past the end of the line is cut there.
LineLayout layoutLine( LineLayoutInput input );

} // namespace klogg::render

#endif // KLOGG_RENDER_LAYOUT_H
