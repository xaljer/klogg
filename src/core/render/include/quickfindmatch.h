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

#ifndef KLOGG_RENDER_QUICKFINDMATCH_H
#define KLOGG_RENDER_QUICKFINDMATCH_H

#include <memory>
#include <string>
#include <string_view>

#include "color.h"
#include "containers.h"
#include "highlightedmatch.h"
#include "pcre2regex.h"

namespace klogg::render {

// Inline matching of the QuickFind pattern, ported from the shell's
// QuickFindPattern::matchLine (quickfindpattern.cpp:186-202) so the visible
// lines highlight exactly as they did before the composition moved into core.
// Only this per-line matching moved: the QuickFind engine (the asynchronous
// incremental search behind the caret) stays in the shell and keeps its own
// compiled expression.
struct QuickFindOptions {
    // The pattern as typed by the user, unescaped.
    std::string pattern;
    bool caseSensitive = true;
    // False for a literal search: the pattern is escaped before compiling,
    // which is what the shell does for a non-regexp QuickFind.
    bool regex = true;
    // Background of a match. The foreground is the fixed one the shell uses
    // (black), so it is not an option.
    Rgba backColor;
};

// Compiled QuickFind pattern. Match columns are UTF-16 code units of the
// display text, as reported by every other contributor to the composition.
class QuickFindMatcher {
public:
    QuickFindMatcher() = default;
    explicit QuickFindMatcher( QuickFindOptions options );

    // Mirrors QuickFindPattern::isActive: an empty pattern, or one that does
    // not compile, matches nothing.
    bool isActive() const
    {
        return active_;
    }

    // Appends the hits of one line to `matches`, which the caller owns so the
    // composition keeps them in the order it needs; returns whether the line
    // has any hit.
    bool matchLine( std::string_view displayText, klogg::vector<HighlightedMatch>& matches ) const;

private:
    friend QuickFindMatcher cachedQuickFindMatcher( const QuickFindOptions& );

    // True when this matcher was compiled for the same expression as
    // `options`; the match colour is not part of the compilation.
    bool matches( const QuickFindOptions& options ) const;

    QuickFindOptions options_;
    std::shared_ptr<const klogg::Pcre2Regex> regexp_;
    bool active_ = false;
};

// Compiled expression of `options`, reused across the lines of a frame and
// across frames: the shell's QuickFindPattern holds one compiled expression
// while its pattern is unchanged, so compiling per line or per frame would be
// a regression. The cache is per thread, which is where the composition runs,
// and keeps a few entries so two views with different patterns do not evict
// each other on every frame.
QuickFindMatcher cachedQuickFindMatcher( const QuickFindOptions& options );

} // namespace klogg::render

#endif // KLOGG_RENDER_QUICKFINDMATCH_H
