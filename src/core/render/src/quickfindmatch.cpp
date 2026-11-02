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

#include "quickfindmatch.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <simdutf.h>

namespace klogg::render {

namespace {

// QuickFindPattern's fixed match foreground (quickfindpattern.cpp:54)
constexpr Rgba QuickFindForeColor{ 0, 0, 0, 255 };

// Mirrors the shell's QRegularExpression options: Unicode properties always,
// case folding when the search is case insensitive.
klogg::Pcre2Regex::PatternOptions patternOptions( bool caseSensitive )
{
    klogg::Pcre2Regex::PatternOptions options = klogg::Pcre2Regex::UseUnicodePropertiesOption;
    if ( !caseSensitive ) {
        options |= klogg::Pcre2Regex::CaseInsensitiveOption;
    }
    return options;
}

bool samePattern( const QuickFindOptions& lhs, const QuickFindOptions& rhs )
{
    return lhs.pattern == rhs.pattern && lhs.caseSensitive == rhs.caseSensitive
           && lhs.regex == rhs.regex;
}

// Compiled expressions of the patterns recently used on this thread, most
// recently used first.
constexpr std::size_t CacheSize = 4;

std::vector<QuickFindMatcher>& cache()
{
    static thread_local std::vector<QuickFindMatcher> entries;
    return entries;
}

} // namespace

QuickFindMatcher::QuickFindMatcher( QuickFindOptions options )
    : options_( std::move( options ) )
{
    // An empty pattern is inactive whatever the engine mode: the shell decides
    // the same way on the raw pattern, before any escaping.
    if ( options_.pattern.empty() ) {
        return;
    }

    auto expression = std::make_shared<klogg::Pcre2Regex>(
        options_.regex ? options_.pattern : klogg::Pcre2Regex::escape( options_.pattern ),
        patternOptions( options_.caseSensitive ) );

    if ( !expression->isValid() ) {
        return;
    }

    regexp_ = std::move( expression );
    active_ = true;
}

bool QuickFindMatcher::matches( const QuickFindOptions& options ) const
{
    return samePattern( options_, options );
}

bool QuickFindMatcher::matchLine( std::string_view displayText,
                                  klogg::vector<HighlightedMatch>& matches ) const
{
    if ( !active_ ) {
        return false;
    }

    const auto matchesBefore = matches.size();

    auto matchIterator = regexp_->globalMatch( displayText );
    while ( matchIterator.hasNext() ) {
        const auto match = matchIterator.next();

        // Byte offsets in the UTF-8 display text; the columns the paint path
        // measures in are UTF-16 code units of that same text.
        const auto startByte = match.capturedStart( 0 );
        const auto endByte = match.capturedEnd( 0 );
        if ( startByte < 0 || endByte < startByte ) {
            continue;
        }

        const auto startColumn
            = static_cast<LineColumn::UnderlyingType>( simdutf::utf16_length_from_utf8(
                displayText.data(), static_cast<size_t>( startByte ) ) );
        const auto endColumn = static_cast<LineColumn::UnderlyingType>(
            simdutf::utf16_length_from_utf8( displayText.data(), static_cast<size_t>( endByte ) ) );

        matches.emplace_back( LineColumn{ startColumn }, LineLength{ endColumn - startColumn },
                              QuickFindForeColor, options_.backColor );
    }

    return matches.size() != matchesBefore;
}

QuickFindMatcher cachedQuickFindMatcher( const QuickFindOptions& options )
{
    auto& entries = cache();

    const auto cachedIt = std::find_if(
        entries.begin(), entries.end(),
        [ &options ]( const QuickFindMatcher& entry ) { return entry.matches( options ); } );

    if ( cachedIt != entries.end() ) {
        // Only the match colour can differ between two hits of this branch: the
        // compiled expression is reused as is.
        auto matcher = *cachedIt;
        matcher.options_ = options;
        return matcher;
    }

    auto matcher = QuickFindMatcher( options );
    entries.insert( entries.begin(), matcher );
    if ( entries.size() > CacheSize ) {
        entries.pop_back();
    }

    return matcher;
}

} // namespace klogg::render
