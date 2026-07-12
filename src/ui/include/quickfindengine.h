/*
 * Copyright (C) 2026 klogg contributors
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

#ifndef KLOGG_QUICKFINDENGINE_H
#define KLOGG_QUICKFINDENGINE_H

#include <functional>

#include "atomicflag.h"
#include "linetypes.h"
#include "selection.h"

class AbstractLogData;
class QuickFindMatcher;

// Stateless scanning core for Quick Find. Given a data source and a matcher,
// it scans for the next/previous match from a start position. It has no Qt
// dependencies beyond value types, no member state, and no side effects other
// than the progress callback, which makes it safe to run concurrently and
// straightforward to unit test.
namespace QuickFindEngine {

struct Result {
    Portion match;            // isValid() only when a match was found
    bool interrupted = false; // true when the scan stopped due to the flag
};

// Called periodically during a scan so the caller can surface progress.
using ProgressFn = std::function<void( LineNumber current, LinesCount total )>;

// Scan forward from startPosition to the end of the data. The first line is
// matched from startPosition.column(); subsequent lines from column 0.
Result searchForward( const AbstractLogData& data, const QuickFindMatcher& matcher,
                      const FilePosition& startPosition, const AtomicFlag& interrupt,
                      const ProgressFn& progress );

// Scan backward from startPosition to the beginning of the data. The first line
// is matched up to startPosition.column(); subsequent lines from the end.
Result searchBackward( const AbstractLogData& data, const QuickFindMatcher& matcher,
                       const FilePosition& startPosition, const AtomicFlag& interrupt,
                       const ProgressFn& progress );

} // namespace QuickFindEngine

#endif // KLOGG_QUICKFINDENGINE_H
