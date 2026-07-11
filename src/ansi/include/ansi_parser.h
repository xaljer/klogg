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

#include <QColor>
#include <QString>

#include "configuration.h"
#include "containers.h"

struct AnsiSegment {
    int startColumn;
    int length;
    QColor foreColor;
};

struct AnsiParseResult {
    QString cleanText;
    klogg::vector<AnsiSegment> segments;
};

// Text whose columns match what is drawn on screen, in a single coordinate
// space defined by the current AnsiProcessing mode.
struct AnsiDisplayLine {
    QString text;
    klogg::vector<AnsiSegment> segments;
};

class AnsiSgrParser {
public:
    static AnsiParseResult parseLine( const QString& line, AnsiProcessing mode,
                                      const QColor& defaultFg, const QColor& defaultBg );

    // Single authority for the display coordinate space. Given an untabified
    // expanded line, returns the text (and color segments) that all producers
    // and consumers of LineColumn must share. The mode is read here so callers
    // cannot pick a divergent one; the no-ANSI fast path avoids any copy.
    static AnsiDisplayLine processDisplayLine( QString expandedLine, const QColor& defaultFg,
                                               const QColor& defaultBg );

private:
    struct AnsiState {
        int fgIndex = -1;
        QColor fgTrueColor;
        bool hasFgTrueColor = false;
        int bgIndex = -1;

        void reset();
        void applyCommand( int cmd );
        QColor resolveForeground( const QColor& defaultFg ) const;
    };

    static QColor colorFrom256( int index );
    static QColor standardColor( int index );
};

#endif // KLOGG_ANSI_PARSER_H
