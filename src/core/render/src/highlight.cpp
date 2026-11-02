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

#include "highlight.h"

namespace klogg::render {

LineHighlightResult composeLineHighlights( const LineHighlightInput& input )
{
    LineHighlightResult result;

    const auto insideSearchLimits = !( input.lineNumber < input.searchStartIndex
                                       || input.lineNumber >= input.searchEndIndex );

    if ( insideSearchLimits && input.highlighterSet ) {
        result.matchType = input.highlighterSet->matchLine( input.line, result.matches );

        if ( result.matchType == HighlighterMatchType::LineMatch && !result.matches.empty() ) {
            result.lineForeColor = result.matches.front().foreColor();
            result.lineBackColor = result.matches.front().backColor();
        }
    }

    // ANSI colours are not limited by the search range: a line outside it keeps
    // its colours, only its highlights are dropped.
    if ( input.ansiProcessing == AnsiProcessing::RenderColors && input.ansiSegments ) {
        for ( const auto& segment : *input.ansiSegments ) {
            result.matches.addMatch( HighlightedMatch{ LineColumn{ segment.startColumn },
                                                       LineLength{ segment.length },
                                                       segment.foreColor, klogg::Rgba{} } );
        }
    }

    if ( insideSearchLimits ) {
        if ( input.patternHighlight ) {
            klogg::vector<HighlightedMatch> patternMatches;
            input.patternHighlight->matchLine( input.line, patternMatches );
            result.matches.addMatches( patternMatches );
        }

        if ( input.quickHighlighters ) {
            for ( const auto& highlighter : *input.quickHighlighters ) {
                klogg::vector<HighlightedMatch> colorLabelMatches;
                highlighter.matchLine( input.line, colorLabelMatches );
                result.matches.addMatches( colorLabelMatches );
            }
        }

        if ( input.externalMatches ) {
            result.matches.addMatches( *input.externalMatches );
        }
    }

    if ( input.selectionPortion ) {
        result.matches.addMatch( *input.selectionPortion );
    }

    return result;
}

} // namespace klogg::render
