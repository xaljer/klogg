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

#ifndef KLOGG_HIGHLIGHTERSETCOLLECTION_H
#define KLOGG_HIGHLIGHTERSETCOLLECTION_H

#include <string>
#include <vector>

#include "containers.h"
#include "highlighterset.h"

// The UI-owned collection of named highlight sets: which sets exist, which are
// active, and the merged effective set the render path consumes. The core only
// ever sees the merged flat rules (a single HighlighterSet); this collection and
// its persistence stay in the UI.
class HighlighterSetCollection final {
public:
    klogg::vector<HighlighterSet> highlighterSets() const;
    void setHighlighterSets( const klogg::vector<HighlighterSet>& highlighters );

    const HighlighterSet& currentActiveSet() const;

    // Recomputes the merged effective set after callers edited the rules of a
    // set in place; the activation mutators already do it themselves
    void rebuildCombinedSet();

    bool hasSet( const std::string& setId ) const;
    bool hasSetByName( const std::string& name ) const;

    std::vector<std::string> activeSetIds() const;
    void activateSet( const std::string& setId );
    void deactivateSet( const std::string& setId );
    void deactivateAll();

    const klogg::vector<QuickHighlighter>& quickHighlighters() const;
    void setQuickHighlighters( const klogg::vector<QuickHighlighter>& quickHighlighters );

private:
    void updateCombinedSet();

private:
    klogg::vector<HighlighterSet> highlighters_;
    std::vector<std::string> activeSets_;
    HighlighterSet combinedActiveSet_;

    klogg::vector<QuickHighlighter> quickHighlighters_;

    // To simplify this class interface, HighlightersDialog can access our
    // internal structure directly.
    friend class HighlightersDialog;
};

#endif // KLOGG_HIGHLIGHTERSETCOLLECTION_H
