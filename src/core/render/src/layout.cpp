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

#include <algorithm>
#include <cstddef>

#include "layout.h"

namespace klogg::render {

namespace {

// Accumulates the inclusive column ranges of one line (the paint path's
// LineDrawer::addChunk: an empty or reversed range is dropped, endColumn is the
// last column written) and turns each of them into the runs to paint, split at
// the wrap parts.
class RunBuilder {
public:
    explicit RunBuilder( const std::vector<WrapPart>& parts )
        : parts_( parts )
    {
    }

    bool empty() const
    {
        return !hasRange_;
    }

    LineColumn endColumn() const
    {
        return lastEndColumn_;
    }

    void addRange( LineColumn firstColumn, LineColumn lastColumn, const Rgba& foreColor,
                   const Rgba& backColor )
    {
        const auto columns = lastColumn.get() - firstColumn.get() + 1;
        if ( columns <= 0 ) {
            return;
        }

        lastEndColumn_ = lastColumn;
        hasRange_ = true;
        appendRuns( firstColumn, static_cast<std::size_t>( columns ), foreColor, backColor );
    }

    std::vector<LineChunk> takeRuns()
    {
        return std::move( runs_ );
    }

private:
    std::size_t partColumns( std::size_t index ) const
    {
        return static_cast<std::size_t>( parts_[ index ].length.get() );
    }

    // Emits the part of the line starting at `position` in part `index`; the
    // first run of a range keeps the row it starts in, every later one moves to
    // the next row.
    void emitRun( std::size_t index, std::size_t position, std::size_t columns,
                  const Rgba& foreColor, const Rgba& backColor )
    {
        const auto start
            = parts_[ index ].start.get() + static_cast<LineColumn::UnderlyingType>( position );
        runs_.push_back( LineChunk{
            LineColumn{ start }, LineLength{ static_cast<LineLength::UnderlyingType>( columns ) },
            foreColor, backColor, !firstRun_ } );
        firstRun_ = false;
    }

    void appendRuns( LineColumn firstColumn, std::size_t columns, const Rgba& foreColor,
                     const Rgba& backColor )
    {
        firstRun_ = true;

        const auto partCount = parts_.size();
        if ( partCount == 0 ) {
            return;
        }

        const auto startColumn = std::max<LineColumn::UnderlyingType>( firstColumn.get(), 0 );

        if ( partCount == 1 ) {
            // Single part line: the tail of the part, and no boundary to move
            // to, so a single run regardless of the range.
            const auto position = static_cast<std::size_t>( startColumn );
            const auto available = partColumns( 0 ) > position ? partColumns( 0 ) - position : 0;
            emitRun( 0, position, std::min( columns, available ), foreColor, backColor );
            return;
        }

        // Multi part line: walk the parts the range covers, emitting a run per
        // part. The first loop stops with the range start in its part (a start
        // exactly on a boundary stays in the previous part and is emitted as an
        // empty run below, which is what moves the cursor).
        std::size_t index = 0;
        auto position = static_cast<std::size_t>( startColumn );
        while ( position > partColumns( index ) ) {
            position -= partColumns( index );
            ++index;
            if ( index >= partCount ) {
                return;
            }
        }

        auto remaining = columns;
        while ( position + remaining > partColumns( index ) ) {
            const auto runColumns = partColumns( index ) - position;
            emitRun( index, position, runColumns, foreColor, backColor );
            ++index;
            position = 0;
            remaining -= runColumns;
            if ( index >= partCount ) {
                return;
            }
        }

        if ( remaining > 0 ) {
            emitRun( index, position, std::min( remaining, partColumns( index ) - position ),
                     foreColor, backColor );
        }
    }

    const std::vector<WrapPart>& parts_;
    std::vector<LineChunk> runs_;
    LineColumn lastEndColumn_;
    bool hasRange_ = false;
    bool firstRun_ = false;
};

} // namespace

LineLayout layoutLine( LineLayoutInput input )
{
    LineLayout layout;
    layout.parts = wrapLine( input.line, input.wrapColumns );

    const auto totalColumns = static_cast<std::size_t>( layout.parts.back().start.get() )
                              + static_cast<std::size_t>( layout.parts.back().length.get() );
    const auto lineColumns = LineColumn{ static_cast<LineColumn::UnderlyingType>( totalColumns ) };

    const bool wrapping = input.wrapColumns.get() > 0;
    const auto firstVisibleColumn
        = wrapping ? 0_lcol : std::clamp( input.firstColumn, 0_lcol, lineColumns );
    const auto lastVisibleColumn
        = wrapping ? lineColumns : input.firstColumn + input.visibleColumns;

    input.highlights.clamp( firstVisibleColumn, lastVisibleColumn );

    RunBuilder builder( layout.parts );

    if ( !input.highlights.empty() && totalColumns > 0 ) {
        if ( input.highlights.front().startColumn() > firstVisibleColumn ) {
            builder.addRange( firstVisibleColumn, input.highlights.front().startColumn() - 1_length,
                              input.foreColor, input.backColor );
        }

        for ( const auto& match : input.highlights.matches() ) {
            const auto matchStart = match.startColumn();

            if ( !builder.empty() && matchStart - builder.endColumn() > 1_length ) {
                builder.addRange( builder.endColumn() + 1_length, matchStart - 1_length,
                                  input.foreColor, input.backColor );
            }

            const auto matchEnd = match.endColumn();
            auto matchLength = match.size();
            if ( matchEnd >= lineColumns ) {
                // A highlight running past the end of the line is cut there.
                const auto columnsFromStart
                    = static_cast<LineLength::UnderlyingType>( totalColumns )
                      - match.startColumn().get();
                matchLength
                    = LineLength{ std::max<LineLength::UnderlyingType>( columnsFromStart, 0 ) };
            }

            if ( matchLength > 0_length ) {
                const auto matchBackColor
                    = match.backColor().a != 0 ? match.backColor() : input.backColor;
                builder.addRange( match.startColumn(), matchEnd, match.foreColor(),
                                  matchBackColor );
            }
        }

        const auto lastHighlightColumn = input.highlights.back().endColumn();
        if ( lastHighlightColumn < lastVisibleColumn ) {
            builder.addRange( lastHighlightColumn + 1_length, lastVisibleColumn, input.foreColor,
                              input.backColor );
        }
    }
    else if ( wrapping ) {
        builder.addRange( 0_lcol, lineColumns, input.foreColor, input.backColor );
    }
    else {
        builder.addRange( input.firstColumn, input.firstColumn + input.visibleColumns,
                          input.foreColor, input.backColor );
    }

    layout.chunks = builder.takeRuns();
    return layout;
}

} // namespace klogg::render
