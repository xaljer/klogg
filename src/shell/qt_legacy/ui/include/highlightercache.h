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

#ifndef KLOGG_HIGHLIGHTERCACHE_H
#define KLOGG_HIGHLIGHTERCACHE_H

#include "containers.h"
#include "highlighterset.h"
#include "highlightersetcollection.h"

namespace klogg::ui {

// UI-side owner of the named highlight sets: the collection holds the sets,
// their active flags and the merged effective set, and the cache pushes the
// merged flat rules plus the color label palette to the core session. The core
// never sees the collection, only the effective rules.
//
// The paint path composes frames through Session::renderViewport, which uses
// the highlight snapshot the session publishes itself. The per-line fallback
// path (KLOGG_RENDER_ENGINE=off), copyWithColor, the color labels and the
// highlighters dialog still resolve their configuration through this cache.
class HighlighterCache {
public:
    static HighlighterCache& instance();

    // Combined and compiled active sets; never touches the worker thread
    // after the initial build
    const HighlighterSet& activeSet();

    // Quick highlighter color label palette, same caching rules as activeSet
    const klogg::vector<QuickHighlighter>& quickHighlighters();

    // The UI-owned collection, loaded from the platform store on first use.
    // Callers that mutate it must call refresh() (recompute + push) and, when
    // the change should survive a restart, persist().
    HighlighterSetCollection& collection();

    // Recomputes the merged effective set and pushes it plus the palette to the
    // core session. Loads the persisted collection first when it never was.
    void refresh();

    // Startup path: loads the persisted collection from the platform store and
    // pushes it to the core session. Called once from the composition root,
    // after the API session exists.
    void applyPersistedState();

    // Persists the UI collection (sets, active flags, palette) back to the
    // platform store. Called after every UI highlight mutation.
    void persist();

private:
    HighlighterCache() = default;

    void ensureLoaded();
    void ensureDefaultQuickHighlighters();
    void pushToSession();

    HighlighterSetCollection collection_;
    bool initialized_ = false;
};

} // namespace klogg::ui

#endif // KLOGG_HIGHLIGHTERCACHE_H
