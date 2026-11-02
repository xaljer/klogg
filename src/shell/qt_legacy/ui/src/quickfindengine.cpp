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

#include "quickfindengine.h"

#include <algorithm>
#include <utility>

#include <QColor>

#include "ansi_parser.h"
#include "configuration.h"
#include "linedatasource.h"
#include "quickfindpattern.h"

// TODO(large-file scan): the loops below scan line by line, and the per-line
// cost is dominated by producing the display text (decode + untabify + ANSI
// strip + QString allocation), not by the regex itself. This is fine for the
// common case (a match nearby, early exit), but for an absent/far pattern it is
// O(file): scanning a multi-GB file serially can take tens of seconds.
//
// Faster worst-case path (do not use RE2; Hyperscan is already available and
// faster, see klogg_regex / LogFilteredDataWorker):
//   1. Prefilter raw file bytes in large blocks with Hyperscan (~GB/s) to rule
//      out blocks/lines that cannot match, WITHOUT producing display text.
//   2. Produce display text + run the precise matcher only on candidate lines
//      (to get exact display-space columns).
//   3. Scan blocks in parallel (TBB), like the main search, but keep forward
//      order so "find next" still returns the earliest match.
// Correctness caveat: a raw-byte prefilter can miss matches that only appear
// after ANSI stripping / untabify (e.g. "a\x1b[0mb" -> "ab"). Fall back to
// precise matching for lines containing ESC or TAB (rare), so the prefilter
// stays conservative.

namespace {

// Block-cached read access to the data source: raw lines are fetched one
// ScanBlockLines-sized block at a time through the batch getLines API, so
// API-backed sources pay one marshal per block instead of one per line.
class BlockLineReader {
public:
    BlockLineReader( const LineDataSource& data, LinesCount nbLines )
        : data_( data )
        , nbLines_( nbLines )
    {
    }

    // Line text in the display coordinate space (tabs expanded, ANSI
    // processed per the current mode) so match columns align with what is
    // rendered, selected and copied.
    QString displayLine( LineNumber line )
    {
        if ( line >= nbLines_ ) {
            return {};
        }
        if ( !blockStart_ || line < *blockStart_
             || line >= *blockStart_ + QuickFindEngine::ScanBlockLines ) {
            fetchBlock( line );
        }

        const auto offset = ( line - *blockStart_ ).get();
        if ( offset >= block_.size() ) {
            return {};
        }

        // A scan visits each line at most once (it advances monotonically),
        // so the raw text can be expanded in place
        return QString::fromStdString( AnsiSgrParser::processDisplayLine(
                                           untabify( std::move( block_[ offset ] ) ).toStdString(),
                                           Configuration::get().ansiProcessing(), klogg::Rgba(),
                                           klogg::Rgba() )
                                           .text );
    }

private:
    void fetchBlock( LineNumber line )
    {
        blockStart_
            = LineNumber( line.get() - ( line.get() % QuickFindEngine::ScanBlockLines.get() ) );
        // Clamp the tail block: native sources pad over-range requests with
        // warning lines, API sources clamp, the scan wants exactly the rest
        const auto remaining = LinesCount( nbLines_.get() - blockStart_->get() );
        const auto lines = data_.getLines( *blockStart_,
                                           std::min( QuickFindEngine::ScanBlockLines, remaining ) );
        block_.clear();
        block_.reserve( lines.size() );
        for ( const auto& rawLine : lines ) {
            block_.push_back( QString::fromStdString( rawLine ) );
        }
    }

    const LineDataSource& data_;
    LinesCount nbLines_;
    OptionalLineNumber blockStart_;
    klogg::vector<QString> block_;
};

} // namespace

QuickFindEngine::Result QuickFindEngine::searchForward( const LineDataSource& data,
                                                        const QuickFindMatcher& matcher,
                                                        const FilePosition& startPosition,
                                                        const AtomicFlag& interrupt,
                                                        const ProgressFn& progress )
{
    const auto nbLines = data.getNbLine();
    BlockLineReader reader( data, nbLines );

    auto line = startPosition.line();

    // Look at the rest of the first line, starting from the requested column.
    if ( matcher.isLineMatching( reader.displayLine( line ), startPosition.column() ) ) {
        const auto [ startCol, endCol ] = matcher.getLastMatch();
        return { Portion{ line, startCol, endCol }, false };
    }

    // Then the rest of the file.
    ++line;
    while ( line < nbLines ) {
        if ( matcher.isLineMatching( reader.displayLine( line ) ) ) {
            const auto [ startCol, endCol ] = matcher.getLastMatch();
            return { Portion{ line, startCol, endCol }, false };
        }
        ++line;

        progress( line, nbLines );

        if ( interrupt ) {
            return { Portion{}, true };
        }
    }

    return { Portion{}, false };
}

QuickFindEngine::Result QuickFindEngine::searchBackward( const LineDataSource& data,
                                                         const QuickFindMatcher& matcher,
                                                         const FilePosition& startPosition,
                                                         const AtomicFlag& interrupt,
                                                         const ProgressFn& progress )
{
    const auto nbLines = data.getNbLine();
    BlockLineReader reader( data, nbLines );

    auto line = startPosition.line();

    // Look at the beginning of the first line, up to the requested column.
    if ( ( startPosition.column() > 0_lcol )
         && matcher.isLineMatchingBackward( reader.displayLine( line ), startPosition.column() ) ) {
        const auto [ startCol, endCol ] = matcher.getLastMatch();
        return { Portion{ line, startCol, endCol }, false };
    }

    if ( line > 0_lnum ) {
        --line;
        while ( true ) {
            if ( matcher.isLineMatchingBackward( reader.displayLine( line ) ) ) {
                const auto [ startCol, endCol ] = matcher.getLastMatch();
                return { Portion{ line, startCol, endCol }, false };
            }
            if ( line == 0_lnum ) {
                break;
            }
            --line;

            progress( line, nbLines );

            if ( interrupt ) {
                return { Portion{}, true };
            }
        }
    }

    return { Portion{}, false };
}
