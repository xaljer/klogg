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

#include <catch2/catch.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "quickfindmatch.h"

// Golden tests for the inline QuickFind matching, which ports the shell's
// QuickFindPattern::matchLine (QString/QRegularExpression) onto Pcre2Regex.
// The expectations transcribe what the paint path highlighted before the
// matching moved into core.
namespace {

using klogg::render::cachedQuickFindMatcher;
using klogg::render::QuickFindMatcher;
using klogg::render::QuickFindOptions;

const klogg::Rgba MatchBack{ 0xff, 0xee, 0x00 };
const klogg::Rgba MatchFore{ 0x00, 0x00, 0x00, 0xff };

QuickFindOptions literalOptions( std::string pattern, bool caseSensitive = true )
{
    QuickFindOptions options;
    options.pattern = std::move( pattern );
    options.caseSensitive = caseSensitive;
    options.regex = false;
    options.backColor = MatchBack;
    return options;
}

QuickFindOptions regexOptions( std::string pattern, bool caseSensitive = true )
{
    QuickFindOptions options;
    options.pattern = std::move( pattern );
    options.caseSensitive = caseSensitive;
    options.regex = true;
    options.backColor = MatchBack;
    return options;
}

// "start:size" per match, in the order the composition appends them
std::vector<std::string> matchList( const QuickFindMatcher& matcher, std::string_view line )
{
    klogg::vector<HighlightedMatch> matches;
    matcher.matchLine( line, matches );

    std::vector<std::string> result;
    for ( const auto& match : matches ) {
        result.push_back( std::to_string( match.startColumn().get() ) + ":"
                          + std::to_string( match.size().get() ) );
    }
    return result;
}

} // namespace

TEST_CASE( "quickfind: a literal pattern matches every occurrence", "[render][quickfindmatch]" )
{
    const QuickFindMatcher matcher{ literalOptions( "WARN" ) };

    REQUIRE( matcher.isActive() );
    REQUIRE( matchList( matcher, "WARN and WARN" ) == std::vector<std::string>{ "0:4", "9:4" } );
    REQUIRE( matchList( matcher, "nothing here" ).empty() );

    klogg::vector<HighlightedMatch> matches;
    matcher.matchLine( "WARN", matches );
    REQUIRE( matches.front().backColor() == MatchBack );
    // The shell paints every QuickFind hit with its own fixed foreground
    REQUIRE( matches.front().foreColor() == MatchFore );
}

TEST_CASE( "quickfind: a literal pattern escapes the regex metacharacters",
           "[render][quickfindmatch]" )
{
    const QuickFindMatcher literal{ literalOptions( "a.c" ) };

    REQUIRE( matchList( literal, "a.c and abc" ) == std::vector<std::string>{ "0:3" } );

    const QuickFindMatcher regex{ regexOptions( "a.c" ) };

    REQUIRE( matchList( regex, "a.c and abc" ) == std::vector<std::string>{ "0:3", "8:3" } );
}

TEST_CASE( "quickfind: case sensitivity follows the search", "[render][quickfindmatch]" )
{
    const QuickFindMatcher sensitive{ literalOptions( "warn" ) };
    REQUIRE( matchList( sensitive, "WARN warn" ) == std::vector<std::string>{ "5:4" } );

    const QuickFindMatcher insensitive{ literalOptions( "warn", false ) };
    REQUIRE( matchList( insensitive, "WARN warn" ) == std::vector<std::string>{ "0:4", "5:4" } );
}

TEST_CASE( "quickfind: nothing is matched without a usable pattern", "[render][quickfindmatch]" )
{
    SECTION( "empty pattern" )
    {
        const QuickFindMatcher matcher{ literalOptions( "" ) };
        REQUIRE_FALSE( matcher.isActive() );
        REQUIRE( matchList( matcher, "anything" ).empty() );
    }

    SECTION( "pattern that does not compile" )
    {
        const QuickFindMatcher matcher{ regexOptions( "a(b" ) };
        REQUIRE_FALSE( matcher.isActive() );
        REQUIRE( matchList( matcher, "a(b" ).empty() );
    }

    SECTION( "default constructed matcher" )
    {
        const QuickFindMatcher matcher;
        REQUIRE_FALSE( matcher.isActive() );
        REQUIRE( matchList( matcher, "anything" ).empty() );
    }
}

TEST_CASE( "quickfind: match columns count UTF-16 code units", "[render][quickfindmatch]" )
{
    // U+8B66 U+8A00, one UTF-16 unit each, then the match; the source stays
    // ASCII so MSVC reads the same bytes whatever the system code page is
    const QuickFindMatcher matcher{ literalOptions( "WARN" ) };

    REQUIRE( matchList( matcher, "\xe8\xad\xa6\xe5\x91\x8a WARN" )
             == std::vector<std::string>{ "3:4" } );
}

TEST_CASE( "quickfind: an empty match advances one code unit at a time",
           "[render][quickfindmatch]" )
{
    const QuickFindMatcher matcher{ regexOptions( "x?" ) };

    REQUIRE( matchList( matcher, "ab" ) == std::vector<std::string>{ "0:0", "1:0", "2:0" } );
}

TEST_CASE( "quickfind: the cache reflects the pattern it is asked for", "[render][quickfindmatch]" )
{
    const auto literal = cachedQuickFindMatcher( literalOptions( "a.c" ) );
    REQUIRE( matchList( literal, "a.c and abc" ) == std::vector<std::string>{ "0:3" } );

    // Same pattern, different mode: the cache must not hand back the literal
    const auto regex = cachedQuickFindMatcher( regexOptions( "a.c" ) );
    REQUIRE( matchList( regex, "a.c and abc" ) == std::vector<std::string>{ "0:3", "8:3" } );

    const auto literalAgain = cachedQuickFindMatcher( literalOptions( "a.c" ) );
    REQUIRE( matchList( literalAgain, "a.c and abc" ) == std::vector<std::string>{ "0:3" } );

    // And the match colour is not part of the compiled expression
    auto recoloured = literalOptions( "a.c" );
    recoloured.backColor = {};
    const auto otherColour = cachedQuickFindMatcher( recoloured );
    klogg::vector<HighlightedMatch> matches;
    otherColour.matchLine( "a.c", matches );
    REQUIRE( matches.size() == 1 );
    REQUIRE( matches.front().backColor().a == 0 );
}
