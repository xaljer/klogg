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

#ifndef KLOGG_RENDER_FRAME_H
#define KLOGG_RENDER_FRAME_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ansi_parser.h"
#include "color.h"
#include "highlight.h"
#include "highlighterset.h"
#include "layout.h"
#include "linetypes.h"
#include "quickfindmatch.h"

namespace klogg::render {

// Palette roles the frame composition needs, already resolved to Rgba by the
// caller that owns the palette.
struct FramePalette {
    // QPalette::Text / QPalette::Base: the colours of a plain line
    Rgba text;
    Rgba base;
    // QPalette::Disabled/QPalette::Text: a line outside the search range
    Rgba dimmedText;
    // QPalette::HighlightedText / QPalette::Highlight: the selected lines and
    // the selected portion of a line
    Rgba selectionText;
    Rgba selectionBack;
};

// The main search pattern highlighting (drawTextArea builds this from the
// configured main search colours), as a set of rule parameters.
struct PatternHighlightOptions {
    std::string pattern;
    bool caseSensitive = false;
    // False for a plain text pattern, which is escaped before compiling
    bool useRegex = true;
    bool variateColors = false;
    Rgba foreColor;
    Rgba backColor;
};

// One colour label slot: the words the user assigned to it and the colours of
// the slot. Slots with no word contribute nothing.
struct QuickHighlighterOptions {
    std::vector<std::string> words;
    Rgba foreColor;
    Rgba backColor;
};

// Selection state of one line of the frame, in frame index order.
struct SelectionPortion {
    // Index of the line inside the frame (0 is the first line passed in)
    std::size_t lineIndex = 0;
    // Columns of the selected portion; unused when wholeLine is set
    LineColumn startColumn = 0_lcol;
    LineLength length = 0_length;
    // True when a multi-line selection covers the whole line: the row takes
    // the selection colours and no highlight is composed for it
    bool wholeLine = false;
};

struct FrameInput {
    // Raw text of the lines of the frame, in view order. Tabs are expanded and
    // ANSI sequences processed here, so the caller passes what the engine
    // returned.
    std::vector<std::string> lines;
    // Gutter flag bits of the same lines (klogg::api::LineFlag values); a
    // shorter or empty vector means "no flags"
    std::vector<uint8_t> lineFlags;
    // View line of each input entry, for callers that compose a set of source
    // lines that is not a contiguous range. Empty means the entries count up
    // from firstLine; entries past the end of a shorter vector fall back to
    // that same numbering.
    std::vector<LineNumber> viewLines;

    // View line of the first entry, which is what the search range is
    // compared against when viewLines is empty
    LineNumber firstLine = 0_lnum;
    // Number shown for the first entry; the following ones count up
    LineNumber firstDisplayLineNumber = 1_lnum;

    // Search range in view lines: start inclusive, end exclusive
    LineNumber searchStartIndex = 0_lnum;
    LineNumber searchEndIndex = 0_lnum;

    // Active highlighter rules. Owned by the caller, which keeps the snapshot
    // alive for the whole call.
    const HighlighterSet* highlighterSet = nullptr;
    AnsiProcessing ansiProcessing = AnsiProcessing::None;

    std::optional<PatternHighlightOptions> patternHighlight;
    std::optional<QuickFindOptions> quickFind;
    std::vector<QuickHighlighterOptions> quickHighlighters;
    std::vector<SelectionPortion> selectionPortions;

    // Window into the line: wrapping width in columns (a non-positive value
    // means no wrapping) plus, when wrapping is off, the first visible column
    // and how many columns are visible from there
    LineColumn wrapColumns = 0_lcol;
    LineColumn firstColumn = 0_lcol;
    LineLength visibleColumns = 0_length;

    FramePalette palette;
};

// One line of a composed frame, ready to paint.
struct FrameLine {
    // Display text: tabs expanded, ANSI sequences stripped, in the UTF-16
    // column space the chunks and the parts are measured in
    std::string displayText;

    // Wrap parts of the line (also the number of visual rows it takes) and the
    // runs to paint, from layoutLine
    std::vector<WrapPart> parts;
    std::vector<LineChunk> chunks;

    // Gutter flags of the line, passed through from the input
    uint8_t flagBits = 0;
    // View line of the entry and the number to print for it
    LineNumber lineNumber;
    LineNumber displayLineNumber;

    // Colours of the parts of the line no chunk covers: the caller fills the
    // row with backColor and paints the chunks on top
    Rgba foreColor;
    Rgba backColor;
};

struct Frame {
    // One entry per input line, in input order
    std::vector<FrameLine> lines;
};

// Composes a whole viewport in one call: for every line it expands the tabs,
// processes the ANSI sequences, composes the highlights and lays the result
// out, exactly as the paint loop used to do line by line.
//
// The steps are the ones of the paint path and stay in its order:
// - the ANSI parser receives the colours of the previous line as its default
//   pair (the paint loop reuses the two colour locals across iterations) and a
//   fully transparent pair for the first line;
// - a line covered by a multi-line selection takes the selection colours and
//   skips the highlight composition altogether;
// - a line outside the search range is dimmed but keeps the highlights the
//   highlighter set produced for it;
// - a whole line rule (HighlighterMatchType::LineMatch) overrides the row
//   foreground and, unlike the older paint loop, the row background too, so
//   its back colour fills the width of the line (see the C5 record in
//   execution-plan.md).
Frame composeFrame( FrameInput input );

} // namespace klogg::render

#endif // KLOGG_RENDER_FRAME_H
