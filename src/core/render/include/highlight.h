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

#ifndef KLOGG_RENDER_HIGHLIGHT_H
#define KLOGG_RENDER_HIGHLIGHT_H

#include <string_view>

#include "ansi_parser.h"
#include "color.h"
#include "highlightedmatch.h"
#include "highlighterset.h"
#include "linetypes.h"

namespace klogg::render {

// Everything that contributes to the highlights of one line, in the order the
// paint path applies it: later contributors overwrite earlier ones.
//
// The paint path has eight ordered steps; this composer covers steps 3 to 8 and
// leaves steps 1 and 2 to the caller, which owns the palette:
//   1. whole-line selection - the caller skips this call entirely
//   2. dimmed foreground outside the search range - the caller picks the colour
//   3. highlighter set rules        (highlighterSet)
//   4. ANSI colour segments         (ansiSegments, ansiProcessing)
//   5. main search pattern          (patternHighlight)
//   6. quick highlighters           (quickHighlighters)
//   7. matches produced outside core (externalMatches)
//   8. selected portion of the line (selectionPortion)
struct LineHighlightInput {
    // Display text of the line: tabs expanded and ANSI codes stripped, as
    // produced for the line by AnsiSgrParser::processDisplayLine. Match columns
    // are UTF-16 code units of this text. May be left empty when the line is
    // outside the search range, because no step reads it then.
    std::string_view line;

    LineNumber lineNumber;
    // Search range: start inclusive, end exclusive.
    LineNumber searchStartIndex;
    LineNumber searchEndIndex;

    // Steps 3 and 5 to 7 only run inside the search range; step 4 runs for the
    // whole file, as ANSI colours describe the line and not a search hit.

    // Active highlighter rules. A rule that highlights the whole line clears
    // everything collected before it.
    const HighlighterSet* highlighterSet = nullptr;
    // Colour segments of the ANSI parser, left to right; applied only when the
    // processing mode renders colours.
    const klogg::vector<AnsiSegment>* ansiSegments = nullptr;
    AnsiProcessing ansiProcessing = AnsiProcessing::None;
    const Highlighter* patternHighlight = nullptr;
    const klogg::vector<Highlighter>* quickHighlighters = nullptr;
    // QuickFind matches until the search engine moves into core, plus any other
    // match the caller produced itself.
    const klogg::vector<HighlightedMatch>* externalMatches = nullptr;
    // Portion of the visual selection covering this line, added last so that it
    // wins over every other contributor.
    const HighlightedMatch* selectionPortion = nullptr;
};

struct LineHighlightResult {
    // Ready for chunk layout: sorted by start column, no overlaps.
    HighlightedMatchRanges matches;
    // What the highlighter set decided for the line.
    HighlighterMatchType matchType = HighlighterMatchType::NoMatch;
    // Colours of the whole-line match, that is matches.front() as the
    // highlighter set left it, before the later steps could split it. Alpha 0
    // means the rule set no such colour, so the caller's default applies.
    klogg::Rgba lineForeColor;
    klogg::Rgba lineBackColor;
};

LineHighlightResult composeLineHighlights( const LineHighlightInput& input );

} // namespace klogg::render

#endif // KLOGG_RENDER_HIGHLIGHT_H
