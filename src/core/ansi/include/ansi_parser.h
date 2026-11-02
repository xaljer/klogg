/*
 * Copyright (C) 2024 Anton Filimonov and other contributors
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

#ifndef KLOGG_ANSI_PARSER_H
#define KLOGG_ANSI_PARSER_H

#include "color.h"
#include "containers.h"
#include "engine_options.h"

struct AnsiSegment {
    // Columns are UTF-16 code units of the display text, like LineColumn and
    // QString::mid(), so a colour segment indexes the same space a search match
    // and the paint path do.
    int startColumn;
    int length;
    klogg::Rgba foreColor;
};

struct AnsiParseResult {
    std::string cleanText;
    klogg::vector<AnsiSegment> segments;
};

// Text whose columns match what is drawn on screen, in a single coordinate
// space defined by the current AnsiProcessing mode.
struct AnsiDisplayLine {
    std::string text;
    klogg::vector<AnsiSegment> segments;
};

class AnsiSgrParser {
public:
    static AnsiParseResult parseLine( const std::string& line, AnsiProcessing mode,
                                      const klogg::Rgba& defaultFg, const klogg::Rgba& defaultBg );

    // Single authority for the display coordinate space. Given an untabified
    // expanded line, returns the text (and color segments) that all producers
    // and consumers of LineColumn must share. The caller supplies the mode so
    // core does not read a global configuration; the no-ANSI fast path avoids
    // any copy.
    static AnsiDisplayLine processDisplayLine( std::string expandedLine, AnsiProcessing mode,
                                               const klogg::Rgba& defaultFg,
                                               const klogg::Rgba& defaultBg );

private:
    struct AnsiState {
        int fgIndex = -1;
        klogg::Rgba fgTrueColor;
        bool hasFgTrueColor = false;
        int bgIndex = -1;

        void reset();
        void applyCommand( int cmd );
        klogg::Rgba resolveForeground( const klogg::Rgba& defaultFg ) const;
    };

    static klogg::Rgba colorFrom256( int index );
    static klogg::Rgba standardColor( int index );
};

#endif // KLOGG_ANSI_PARSER_H
