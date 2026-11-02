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

#ifndef KLOGG_LINEDATASOURCE_H
#define KLOGG_LINEDATASOURCE_H

#include <string>

#include "containers.h"
#include "linetypes_qt.h"

// Read-only line fetching surface of a view's data set (B4). The method names
// mirror the AbstractLogData read API so call sites read the same. The base
// implementation in AbstractLogView returns empty defaults; LogMainView (B4)
// and FilteredView (B5b) override them to fetch through the Core API session.
// Selection takes this interface instead of AbstractLogData.
class LineDataSource {
public:
    virtual ~LineDataSource() = default;

    // Raw line text (tabs and ANSI escape sequences kept)
    virtual std::string getLineString( LineNumber line ) const = 0;
    // Line text with tabs expanded
    virtual std::string getExpandedLineString( LineNumber line ) const = 0;
    // Raw text of the `number` lines starting at `firstLine`
    virtual klogg::vector<std::string> getLines( LineNumber firstLine,
                                                 LinesCount number ) const = 0;
    // File line number displayed for the line at `index` (1-based)
    virtual LineNumber getLineNumber( LineNumber index ) const = 0;
    virtual LinesCount getNbLine() const = 0;
    // Visible length (tabs expanded) of the longest line
    virtual LineLength getMaxLength() const = 0;
    // Visible length (tabs expanded) of one line
    virtual LineLength getLineLength( LineNumber line ) const = 0;
};

#endif // KLOGG_LINEDATASOURCE_H
