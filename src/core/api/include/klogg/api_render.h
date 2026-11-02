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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "klogg/api_types.h"

#include "engine_options.h"
#include "frame.h"

// Viewport composition surface: everything a consumer needs to paint a frame
// of a file, without the paint path having to know how lines are fetched,
// highlighted and wrapped, plus the mapping that keeps an integer scroll range
// in step with the file's lines.
namespace klogg::api {

// Per-frame parameters of the composing consumer. Everything here is a value
// the consumer owns (its palette, its viewport window, its selection, its
// pattern highlighters); the highlight rules and the colour label palette come
// from the session state instead.
struct ViewportOptions {
    // Wrapping width in columns; a non-positive value means no wrapping, and
    // the window below applies
    LineColumn wrapColumns = 0_lcol;
    // Columns of the display text the view shows, and where it starts
    LineColumn firstColumn = 0_lcol;
    LineLength visibleColumns = 0_length;

    // Search range in view lines: start inclusive, end exclusive. Lines
    // outside it are dimmed and only the ANSI colours are applied to them.
    LineNumber searchStartIndex = 0_lnum;
    LineNumber searchEndIndex = 0_lnum;

    // Number shown for the first line of the frame; the following ones count
    // up from there
    LineNumber firstDisplayLineNumber = 1_lnum;

    // How the consumer renders ANSI escape sequences (its engine option)
    AnsiProcessing ansiProcessing = AnsiProcessing::None;

    // Palette roles of the frame
    klogg::render::FramePalette palette;

    // Selection of the consumer, by frame line index
    std::vector<klogg::render::SelectionPortion> selectionPortions;

    // Main search pattern highlighting; unset when the consumer shows none
    std::optional<klogg::render::PatternHighlightOptions> patternHighlight;
    // QuickFind pattern highlighting of the visible lines; the incremental
    // search itself stays with the consumer
    std::optional<klogg::render::QuickFindOptions> quickFind;
    // Colour labels: the words of each slot, in slot order. The colours come
    // from the session state, which is where the slots are configured; slots
    // beyond the configured ones are ignored.
    std::vector<std::vector<std::string>> quickHighlighterWords;
};

// One composed line: the text to paint plus everything needed to lay it out
// and colour it.
struct ViewportLine {
    // Display text of the line: tabs expanded, ANSI sequences stripped, the
    // text a byte offset in the file maps into
    std::string text;
    // Wrap parts of the line, in order; at least one
    std::vector<klogg::render::WrapPart> parts;
    // Runs to paint, in paint order. Each is a slice of `text` in UTF-16
    // columns, and a run that starts a row moves the painter to the next one.
    std::vector<klogg::render::LineChunk> chunks;

    // View line this entry came from and the number to print for it
    LineNumber lineNumber = 0_lnum;
    LineNumber displayLineNumber = 0_lnum;

    // Gutter flag bits (LineFlag values)
    uint8_t flags = 0;

    // Colours of everything no chunk covers, that is of the blank tail of the
    // line and of the background of the row
    klogg::Rgba foreColor;
    klogg::Rgba backColor;
};

struct ViewportResult {
    // One entry per requested line, in order. Lines past the end of the file
    // come back empty, so a consumer can index by its own row count.
    std::vector<ViewportLine> lines;
};

// Parameters of a copy-with-colour render: the same compositions a viewport
// paints, serialized for the clipboard instead of for a widget. The palette is
// still needed because it is the fallback colour of everything no highlight
// covers and the reference the serializer compares chunk colours against.
struct HtmlRenderOptions {
    // Palette roles of the copied lines
    klogg::render::FramePalette palette;

    // How ANSI escape sequences are rendered; matches the consumer's engine
    // option so the copied text and colours are the ones on screen
    AnsiProcessing ansiProcessing = AnsiProcessing::None;

    // Main search pattern highlighting; unset when the consumer shows none
    std::optional<klogg::render::PatternHighlightOptions> patternHighlight;

    // Colour labels: the words of each slot, in slot order. The colours come
    // from the session state, which is where the slots are configured; slots
    // beyond the configured ones are ignored.
    std::vector<std::vector<std::string>> quickHighlighterWords;

    // Search range in view lines: start inclusive, end exclusive, same
    // semantics as ViewportOptions. Lines outside it keep only their ANSI
    // colours.
    LineNumber searchStartIndex = 0_lnum;
    LineNumber searchEndIndex = 0_lnum;

    // Whether the session highlighter rules contribute highlights. The copy
    // action of the Qt shell does not run them, so this defaults to off and
    // only the pattern highlight and the colour labels apply.
    bool includeHighlighterSet = false;
};

// The clipboard forms of a set of lines: the coloured HTML fragment the Qt
// copy-with-colour produced, plus the same lines as plain text.
struct RenderedHtml {
    std::string html;
    std::string text;
};

// Compression factor between line numbers and the positions of an integer
// scroll range (a scroll bar, a slider): 1.0 while the line count fits an int
// (int_max lines included), and int_max / lines past that, so the whole file
// stays reachable. An empty file is also 1.0: there is nothing to compress,
// and a zero factor would collapse the mapping. Session::scrollMultiplier is
// the file-backed form; a consumer that addresses a line space of its own (a
// filtered view's projection) uses this directly.
double scrollMultiplierForLines( LinesCount lines );

} // namespace klogg::api
