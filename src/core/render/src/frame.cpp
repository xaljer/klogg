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

#include "frame.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace klogg::render {

namespace {

// The paint loop passes its colour locals to the ANSI parser, and a colour
// that reached them through the shell comes back from an invalid QColor as a
// fully transparent black when its alpha was 0. Kept so the two paths colour
// ANSI segments identically.
Rgba asCarriedAnsiColor( const Rgba& color )
{
    return color.a == 0 ? Rgba{} : color;
}

const SelectionPortion* findSelectionPortion( const std::vector<SelectionPortion>& portions,
                                              std::size_t lineIndex )
{
    const auto portionIt = std::find_if( portions.begin(), portions.end(),
                                         [ lineIndex ]( const SelectionPortion& portion ) {
                                             return portion.lineIndex == lineIndex;
                                         } );
    return portionIt != portions.end() ? &*portionIt : nullptr;
}

klogg::vector<Highlighter>
buildQuickHighlighters( const std::vector<QuickHighlighterOptions>& slots )
{
    klogg::vector<Highlighter> highlighters;
    for ( const auto& slot : slots ) {
        highlighters.reserve( highlighters.size() + slot.words.size() );
        for ( const auto& word : slot.words ) {
            // The colour labels are plain text rules that highlight the match
            // only, never the whole line
            Highlighter highlighter{ word, false, true, slot.foreColor, slot.backColor };
            highlighter.setUseRegex( false );
            highlighters.push_back( std::move( highlighter ) );
        }
    }
    return highlighters;
}

std::optional<Highlighter> buildPatternHighlight( const PatternHighlightOptions& options )
{
    if ( options.pattern.empty() ) {
        return std::nullopt;
    }

    Highlighter highlighter;
    highlighter.setHighlightOnlyMatch( true );
    highlighter.setVariateColors( options.variateColors );
    highlighter.setPattern( options.pattern );
    highlighter.setIgnoreCase( !options.caseSensitive );
    highlighter.setUseRegex( options.useRegex );
    highlighter.setBackColor( options.backColor );
    highlighter.setForeColor( options.foreColor );

    return highlighter;
}

} // namespace

Frame composeFrame( FrameInput input )
{
    Frame frame;
    frame.lines.reserve( input.lines.size() );

    const auto patternHighlight
        = input.patternHighlight ? buildPatternHighlight( *input.patternHighlight ) : std::nullopt;
    const auto quickHighlighters = buildQuickHighlighters( input.quickHighlighters );
    const auto quickFindMatcher
        = input.quickFind ? cachedQuickFindMatcher( *input.quickFind ) : QuickFindMatcher{};

    // Defaults the ANSI parser starts from; every later line receives the
    // colours the previous one ended up with (see asCarriedAnsiColor)
    Rgba ansiForeColor;
    Rgba ansiBackColor;

    // Reused across the lines: the composition clears it per line
    klogg::vector<HighlightedMatch> quickFindMatches;

    for ( std::size_t index = 0; index < input.lines.size(); ++index ) {
        const auto indexValue = static_cast<LineNumber::UnderlyingType>( index );
        const auto lineNumber = index < input.viewLines.size()
                                    ? input.viewLines[ index ]
                                    : LineNumber{ input.firstLine.get() + indexValue };

        FrameLine line;
        line.lineNumber = lineNumber;
        line.displayLineNumber = LineNumber{ input.firstDisplayLineNumber.get() + indexValue };
        line.flagBits = index < input.lineFlags.size() ? input.lineFlags[ index ] : uint8_t{ 0 };

        auto expandedLine = untabify( std::move( input.lines[ index ] ) );
        auto ansiDisplay = AnsiSgrParser::processDisplayLine(
            std::move( expandedLine ), input.ansiProcessing, ansiForeColor, ansiBackColor );
        line.displayText = std::move( ansiDisplay.text );

        const auto* selectionPortion = findSelectionPortion( input.selectionPortions, index );
        const bool wholeLineSelected = selectionPortion != nullptr && selectionPortion->wholeLine;

        if ( wholeLineSelected ) {
            line.foreColor = input.palette.selectionText;
            line.backColor = input.palette.selectionBack;
        }
        else {
            line.foreColor = input.palette.text;
            line.backColor = input.palette.base;

            // Outside the search range the text is dimmed, and the dimming
            // wins over the highlighter set (which does not run there at all)
            if ( lineNumber < input.searchStartIndex || lineNumber >= input.searchEndIndex ) {
                line.foreColor = input.palette.dimmedText;
            }
        }

        HighlightedMatchRanges highlights;

        if ( !wholeLineSelected ) {
            std::optional<HighlightedMatch> selectionMatch;
            if ( selectionPortion != nullptr ) {
                selectionMatch.emplace( selectionPortion->startColumn, selectionPortion->length,
                                        input.palette.selectionText, input.palette.selectionBack );
            }

            quickFindMatches.clear();
            quickFindMatcher.matchLine( line.displayText, quickFindMatches );

            LineHighlightInput highlightInput;
            highlightInput.line = line.displayText;
            highlightInput.lineNumber = lineNumber;
            highlightInput.searchStartIndex = input.searchStartIndex;
            highlightInput.searchEndIndex = input.searchEndIndex;
            highlightInput.highlighterSet = input.highlighterSet;
            highlightInput.ansiSegments = &ansiDisplay.segments;
            highlightInput.ansiProcessing = input.ansiProcessing;
            highlightInput.patternHighlight = patternHighlight ? &*patternHighlight : nullptr;
            highlightInput.quickHighlighters = &quickHighlighters;
            highlightInput.externalMatches = &quickFindMatches;
            highlightInput.selectionPortion = selectionMatch ? &*selectionMatch : nullptr;

            auto highlightResult = composeLineHighlights( highlightInput );
            highlights = std::move( highlightResult.matches );

            if ( highlightResult.matchType == HighlighterMatchType::LineMatch ) {
                // Alpha 0 means the rule set no such colour, so the palette one
                // applies
                if ( highlightResult.lineForeColor.a != 0 ) {
                    line.foreColor = highlightResult.lineForeColor;
                }
                if ( highlightResult.lineBackColor.a != 0 ) {
                    line.backColor = highlightResult.lineBackColor;
                }
            }
        }

        LineLayoutInput layoutInput;
        layoutInput.line = line.displayText;
        layoutInput.highlights = std::move( highlights );
        layoutInput.wrapColumns = input.wrapColumns;
        layoutInput.firstColumn = input.firstColumn;
        layoutInput.visibleColumns = input.visibleColumns;
        layoutInput.foreColor = line.foreColor;
        layoutInput.backColor = line.backColor;

        auto layout = layoutLine( std::move( layoutInput ) );
        line.parts = std::move( layout.parts );
        line.chunks = std::move( layout.chunks );

        ansiForeColor = asCarriedAnsiColor( line.foreColor );
        ansiBackColor = asCarriedAnsiColor( line.backColor );

        frame.lines.push_back( std::move( line ) );
    }

    return frame;
}

} // namespace klogg::render
