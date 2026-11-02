/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
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

#include "regularexpression.h"

#include <cstddef>
#include <iterator>
#include <string_view>
#include <variant>

// Expectations were frozen from the QRegularExpression reference this scenario
// used to compare against; the Qt engine is no longer needed to verify them.
SCENARIO( "Fallback regex engine matches expected results", "[fallback]" )
{
    const std::string_view subjects[] = { "This is matching", "a match here",   "ERROR happened",
                                          "café au lait",     "plain a.b text", "no hits inside" };

    const bool expectedMatches[] = { true, true, false, false, false, false };

    WHEN( "Using a regex pattern" )
    {
        const RegularExpressionPattern pattern( "match", true, false, false, false );
        RegularExpression expression{ pattern };
        REQUIRE( expression.isValid() );
        const auto matcher = expression.createMatcher();

        for ( size_t i = 0; i < std::size( subjects ); ++i ) {
            REQUIRE( matcher->hasMatch( subjects[ i ] ) == expectedMatches[ i ] );
        }
    }

    WHEN( "Using plain text pattern" )
    {
        const RegularExpressionPattern pattern( "match", true, false, false, true );
        RegularExpression expression{ pattern };
        REQUIRE( expression.isValid() );
        const auto matcher = expression.createMatcher();

        for ( size_t i = 0; i < std::size( subjects ); ++i ) {
            REQUIRE( matcher->hasMatch( subjects[ i ] ) == expectedMatches[ i ] );
        }

        REQUIRE( matcher->hasMatch( "This is matching" ) );
        REQUIRE_FALSE( matcher->hasMatch( "no hits inside" ) );
    }

    WHEN( "Ignoring case" )
    {
        const RegularExpressionPattern pattern( "error", false, false, false, false );
        RegularExpression expression{ pattern };
        const auto matcher = expression.createMatcher();

        REQUIRE( matcher->hasMatch( "ERROR happened" ) );
        REQUIRE_FALSE( matcher->hasMatch( "no hits inside" ) );
    }

    WHEN( "Matching non-ASCII UTF-8 data" )
    {
        const RegularExpressionPattern pattern( "é", true, false, false, false );
        RegularExpression expression{ pattern };
        const auto matcher = expression.createMatcher();

        REQUIRE( matcher->hasMatch( std::string_view( "café au lait" ) ) );
    }

    WHEN( "Escaping plain text with metacharacters" )
    {
        const RegularExpressionPattern pattern( "a.b", true, false, false, true );
        RegularExpression expression{ pattern };
        const auto matcher = expression.createMatcher();

        REQUIRE( matcher->hasMatch( "plain a.b text" ) );
        REQUIRE_FALSE( matcher->hasMatch( "plain axb text" ) );
    }

    WHEN( "Subject contains invalid UTF-8" )
    {
#ifndef KLOGG_HAS_HS
        // Pcre2Regex rejects invalid UTF-8, while the old QString::fromUtf8
        // path replaced bad bytes with U+FFFD and could still match. Hyperscan
        // matches byte-wise instead, so this only describes the fallback.
        const RegularExpressionPattern pattern( "bad", true, false, false, true );
        RegularExpression expression{ pattern };
        REQUIRE( expression.isValid() );
        const auto matcher = expression.createMatcher();

        REQUIRE_FALSE( matcher->hasMatch( std::string_view( "\xFF"
                                                            "bad",
                                                            4 ) ) );
#endif
    }

    WHEN( "Pattern is invalid" )
    {
        RegularExpression expression{ RegularExpressionPattern( "(", true, false, false, false ) };
        REQUIRE_FALSE( expression.isValid() );
        REQUIRE_FALSE( expression.errorString().empty() );
    }
}

SCENARIO( "Multi-pattern fallback matcher", "[fallback]" )
{
    WHEN( "Using several patterns" )
    {
        klogg::vector<RegularExpressionPattern> patterns{
            RegularExpressionPattern( "foo", true, false, false, false ),
            RegularExpressionPattern( "bar", true, false, false, false ),
        };
        MultiRegularExpression expression{ patterns };
        REQUIRE( expression.isValid() );

        const auto matcher = expression.createMatcher();
        const auto firstResult = matcher->match( "foo line" );
        REQUIRE( firstResult.size() == 2 );
        REQUIRE( firstResult[ 0 ].second );
        REQUIRE_FALSE( firstResult[ 1 ].second );

        const auto secondResult = matcher->match( "bar line" );
        REQUIRE_FALSE( secondResult[ 0 ].second );
        REQUIRE( secondResult[ 1 ].second );
    }
}

SCENARIO( "HsRegularExpression fallback", "[fallback]" )
{
    WHEN( "Using valid patterns" )
    {
        HsRegularExpression expression{ klogg::vector<RegularExpressionPattern>{
            RegularExpressionPattern( "match", true, false, false, false ) } };
        REQUIRE( expression.isValid() );
        REQUIRE( expression.errorString().empty() );

        const auto matcher = expression.createMatcher();
        const auto* defaultMatcher = std::get_if<DefaultRegularExpressionMatcher>( &matcher );
        if ( defaultMatcher == nullptr ) {
            // Hyperscan builds hold the HS matcher here; this scenario covers the
            // fallback engine only.
            SUCCEED( "fallback matcher is not active in this build" );
        }
        else {
            REQUIRE( ( *defaultMatcher ).match( "a match here" )[ 0 ] > 0 );
            REQUIRE( ( *defaultMatcher ).match( "none" )[ 0 ] == 0 );
        }
    }

    WHEN( "Using invalid pattern" )
    {
        HsRegularExpression expression{ klogg::vector<RegularExpressionPattern>{
            RegularExpressionPattern( "(", true, false, false, false ) } };
        REQUIRE_FALSE( expression.isValid() );
        REQUIRE_FALSE( expression.errorString().empty() );
    }
}

SCENARIO( "RegularExpressionPattern converts to Pcre2Regex", "[fallback]" )
{
    WHEN( "Escaping plain text" )
    {
        const RegularExpressionPattern pattern( "a.b", true, false, false, true );
        const auto regex = static_cast<klogg::Pcre2Regex>( pattern );
        REQUIRE( regex.isValid() );
        REQUIRE( regex.match( "a.b" ).hasMatch() );
        REQUIRE_FALSE( regex.match( "axb" ).hasMatch() );
    }

    WHEN( "Respecting case sensitivity" )
    {
        const RegularExpressionPattern caseSensitivePattern( "ERROR", true, false, false, false );
        const auto caseSensitiveRegex = static_cast<klogg::Pcre2Regex>( caseSensitivePattern );
        REQUIRE( caseSensitiveRegex.match( "ERROR" ).hasMatch() );
        REQUIRE_FALSE( caseSensitiveRegex.match( "error" ).hasMatch() );

        const RegularExpressionPattern insensitivePattern( "ERROR", false, false, false, false );
        const auto insensitiveRegex = static_cast<klogg::Pcre2Regex>( insensitivePattern );
        REQUIRE( insensitiveRegex.match( "error" ).hasMatch() );
    }

    WHEN( "Using an invalid pattern" )
    {
        const RegularExpressionPattern pattern( "(", true, false, false, false );
        const auto regex = static_cast<klogg::Pcre2Regex>( pattern );
        REQUIRE_FALSE( regex.isValid() );
        REQUIRE_FALSE( regex.errorString().empty() );
    }
}

SCENARIO( "Pattern matcher in boolean mode", "[patternmatcher]" )
{
    std::string_view matchLine = "\"This\" is matching pattern";

    WHEN( "Using single pattern" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"matching\"", false, false, true, true ) );
        const auto matcher = expression.createMatcher();
        REQUIRE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using complex pattern" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"not_match\" | \"match\"", false, false, true, true ) );
        const auto matcher = expression.createMatcher();
        REQUIRE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using complex pattern with ()" )
    {
        RegularExpression expression( RegularExpressionPattern(
            "(\"not_match\" | \"match\") & !(\"pattern\")", false, false, true, false ) );
        const auto matcher = expression.createMatcher();
        REQUIRE_FALSE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using pattern with escaped quotes" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"\\\"This\\\"\"", false, false, true, false ) );
        const auto matcher = expression.createMatcher();
        REQUIRE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using pattern with not matched quotes" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"not_match\" | \"match", false, false, true, false ) );

        REQUIRE_FALSE( expression.isValid() );
    }
}
