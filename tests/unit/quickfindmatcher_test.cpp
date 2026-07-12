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

// Characterization tests for QuickFindMatcher. These lock down the current
// forward/backward matching behavior so the planned QuickFind refactor can be
// verified against a stable reference. getLastMatch returns inclusive columns:
// start = capturedStart, end = capturedEnd - 1.

#include <QRegularExpression>

#include <catch2/catch.hpp>

#include "quickfindpattern.h"

static QuickFindMatcher makeMatcher( const QString& pattern, bool isRegex = false )
{
    QRegularExpression re( isRegex ? pattern : QRegularExpression::escape( pattern ),
                           QRegularExpression::UseUnicodePropertiesOption );
    REQUIRE( re.isValid() );
    return QuickFindMatcher( !pattern.isEmpty(), re );
}

// Plain-text matcher: exercises the lastIndexOf fast path in backward search.
static QuickFindMatcher makePlainMatcher( const QString& text,
                                          Qt::CaseSensitivity cs = Qt::CaseSensitive )
{
    QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
    if ( cs == Qt::CaseInsensitive ) {
        options |= QRegularExpression::CaseInsensitiveOption;
    }
    QRegularExpression re( QRegularExpression::escape( text ), options );
    REQUIRE( re.isValid() );
    return QuickFindMatcher( !text.isEmpty(), re, text, cs );
}

// "error: something went wrong at line 42"
//  0         1         2         3
//  0123456789012345678901234567890123456789
static const QString kLine = QStringLiteral( "error: something went wrong at line 42" );

SCENARIO( "QuickFindMatcher forward matching", "[quickfind]" )
{
    GIVEN( "A plain-text matcher" )
    {
        WHEN( "pattern is active and present at the start" )
        {
            auto matcher = makeMatcher( QStringLiteral( "error" ) );
            THEN( "it finds the match at columns 0..4" )
            {
                REQUIRE( matcher.isLineMatching( kLine ) );
                auto [ start, end ] = matcher.getLastMatch();
                REQUIRE( start == 0_lcol );
                REQUIRE( end == 4_lcol );
            }
        }

        WHEN( "searching from a column before a later match" )
        {
            auto matcher = makeMatcher( QStringLiteral( "wrong" ) );
            THEN( "it finds 'wrong' at columns 22..26" )
            {
                REQUIRE( matcher.isLineMatching( kLine, 10_lcol ) );
                auto [ start, end ] = matcher.getLastMatch();
                REQUIRE( start == 22_lcol );
                REQUIRE( end == 26_lcol );
            }
        }

        WHEN( "the only occurrence is before the start column" )
        {
            auto matcher = makeMatcher( QStringLiteral( "error" ) );
            THEN( "no match is found" )
            {
                REQUIRE_FALSE( matcher.isLineMatching( kLine, 10_lcol ) );
            }
        }

        WHEN( "the pattern is absent" )
        {
            auto matcher = makeMatcher( QStringLiteral( "unicorn" ) );
            THEN( "no match" )
            {
                REQUIRE_FALSE( matcher.isLineMatching( kLine ) );
            }
        }

        WHEN( "the matcher is inactive (default constructed)" )
        {
            QuickFindMatcher matcher;
            THEN( "no match" )
            {
                REQUIRE_FALSE( matcher.isLineMatching( QStringLiteral( "anything" ) ) );
            }
        }

        WHEN( "there are multiple occurrences" )
        {
            const QString multi = QStringLiteral( "abc abc abc" );
            auto matcher = makeMatcher( QStringLiteral( "abc" ) );

            THEN( "from column 0 it finds the first occurrence" )
            {
                REQUIRE( matcher.isLineMatching( multi ) );
                auto [ start, end ] = matcher.getLastMatch();
                REQUIRE( start == 0_lcol );
                REQUIRE( end == 2_lcol );
            }

            THEN( "from column 4 it finds the second occurrence" )
            {
                REQUIRE( matcher.isLineMatching( multi, 4_lcol ) );
                auto [ start, end ] = matcher.getLastMatch();
                REQUIRE( start == 4_lcol );
                REQUIRE( end == 6_lcol );
            }
        }
    }

    GIVEN( "A regex matcher" )
    {
        WHEN( "using a wildcard pattern" )
        {
            auto matcher = makeMatcher( QStringLiteral( "wr.ng" ), true );
            THEN( "it finds 'wrong' at columns 22..26" )
            {
                REQUIRE( matcher.isLineMatching( kLine ) );
                auto [ start, end ] = matcher.getLastMatch();
                REQUIRE( start == 22_lcol );
                REQUIRE( end == 26_lcol );
            }
        }

        WHEN( "matching digits from column 30" )
        {
            auto matcher = makeMatcher( QStringLiteral( "\\d+" ), true );
            THEN( "it finds '42' at columns 36..37" )
            {
                REQUIRE( matcher.isLineMatching( kLine, 30_lcol ) );
                auto [ start, end ] = matcher.getLastMatch();
                REQUIRE( start == 36_lcol );
                REQUIRE( end == 37_lcol );
            }
        }
    }
}

SCENARIO( "QuickFindMatcher backward matching", "[quickfind]" )
{
    GIVEN( "A plain-text matcher" )
    {
        WHEN( "searching backward from the end of the line (default column)" )
        {
            auto matcher = makeMatcher( QStringLiteral( "line" ) );
            THEN( "it finds 'line' at columns 31..34" )
            {
                REQUIRE( matcher.isLineMatchingBackward( kLine ) );
                auto [ start, end ] = matcher.getLastMatch();
                REQUIRE( start == 31_lcol );
                REQUIRE( end == 34_lcol );
            }
        }

        WHEN( "the match ends after the requested column" )
        {
            auto matcher = makeMatcher( QStringLiteral( "went" ) );
            THEN( "no match is found (search stops before it)" )
            {
                REQUIRE_FALSE( matcher.isLineMatchingBackward( kLine, 15_lcol ) );
            }
        }

        WHEN( "multiple occurrences, searching backward from column 8" )
        {
            const QString multi = QStringLiteral( "abc abc abc" );
            auto matcher = makeMatcher( QStringLiteral( "abc" ) );
            THEN( "it finds the occurrence ending at or before column 8" )
            {
                REQUIRE( matcher.isLineMatchingBackward( multi, 8_lcol ) );
                auto [ start, end ] = matcher.getLastMatch();
                REQUIRE( start == 4_lcol );
                REQUIRE( end == 6_lcol );
            }
        }

        WHEN( "searching backward with column 0" )
        {
            auto matcher = makeMatcher( QStringLiteral( "error" ) );
            THEN( "no match - column 0 precedes the match end" )
            {
                REQUIRE_FALSE( matcher.isLineMatchingBackward( kLine, 0_lcol ) );
            }
        }

        WHEN( "the matcher is inactive" )
        {
            QuickFindMatcher matcher;
            THEN( "no match" )
            {
                REQUIRE_FALSE(
                    matcher.isLineMatchingBackward( QStringLiteral( "anything" ) ) );
            }
        }
    }

    GIVEN( "A regex matcher" )
    {
        WHEN( "matching digits backward from the end" )
        {
            const QString digits = QStringLiteral( "abc12 def34 ghi56" );
            auto matcher = makeMatcher( QStringLiteral( "\\d+" ), true );
            THEN( "it finds the last run '56' at columns 15..16" )
            {
                REQUIRE( matcher.isLineMatchingBackward( digits ) );
                auto [ start, end ] = matcher.getLastMatch();
                REQUIRE( start == 15_lcol );
                REQUIRE( end == 16_lcol );
            }
        }
    }
}

SCENARIO( "QuickFindMatcher edge cases", "[quickfind]" )
{
    GIVEN( "A zero-length (anchor) pattern" )
    {
        auto matcher = makeMatcher( QStringLiteral( "^" ), true );
        THEN( "it matches at column 0 with an inclusive end of -1" )
        {
            REQUIRE( matcher.isLineMatching( QStringLiteral( "hello" ) ) );
            auto [ start, end ] = matcher.getLastMatch();
            REQUIRE( start == 0_lcol );
            REQUIRE( end == LineColumn{ -1 } );
        }
    }

    GIVEN( "An empty input line" )
    {
        auto forward = makeMatcher( QStringLiteral( "x" ) );
        auto backward = makeMatcher( QStringLiteral( "x" ) );
        THEN( "neither forward nor backward matches" )
        {
            REQUIRE_FALSE( forward.isLineMatching( QString{} ) );
            REQUIRE_FALSE( backward.isLineMatchingBackward( QString{} ) );
        }
    }

    GIVEN( "A line with non-ASCII characters" )
    {
        const QString line = QString::fromUtf16( u"\u4f60\u597d hello \u4e16\u754c" );
        auto matcher = makeMatcher( QStringLiteral( "hello" ) );
        THEN( "columns account for each code unit - 'hello' at 3..7" )
        {
            REQUIRE( matcher.isLineMatching( line ) );
            auto [ start, end ] = matcher.getLastMatch();
            REQUIRE( start == 3_lcol );
            REQUIRE( end == 7_lcol );
        }
    }
}

SCENARIO( "QuickFindMatcher plain-text backward matches the regex path", "[quickfind]" )
{
    GIVEN( "The same cases as the regex backward tests, via the fast path" )
    {
        WHEN( "searching backward from the end of the line" )
        {
            auto matcher = makePlainMatcher( QStringLiteral( "line" ) );
            THEN( "it finds 'line' at columns 31..34, same as the regex path" )
            {
                REQUIRE( matcher.isLineMatchingBackward( kLine ) );
                auto [ start, end ] = matcher.getLastMatch();
                REQUIRE( start == 31_lcol );
                REQUIRE( end == 34_lcol );
            }
        }

        WHEN( "the match ends after the requested column" )
        {
            auto matcher = makePlainMatcher( QStringLiteral( "went" ) );
            THEN( "no match is found" )
            {
                REQUIRE_FALSE( matcher.isLineMatchingBackward( kLine, 15_lcol ) );
            }
        }

        WHEN( "multiple occurrences, searching backward from column 8" )
        {
            const QString multi = QStringLiteral( "abc abc abc" );
            auto matcher = makePlainMatcher( QStringLiteral( "abc" ) );
            THEN( "it finds the occurrence ending at or before column 8" )
            {
                REQUIRE( matcher.isLineMatchingBackward( multi, 8_lcol ) );
                auto [ start, end ] = matcher.getLastMatch();
                REQUIRE( start == 4_lcol );
                REQUIRE( end == 6_lcol );
            }
        }

        WHEN( "searching backward with column 0" )
        {
            auto matcher = makePlainMatcher( QStringLiteral( "error" ) );
            THEN( "no match - column 0 precedes the match end" )
            {
                REQUIRE_FALSE( matcher.isLineMatchingBackward( kLine, 0_lcol ) );
            }
        }
    }

    GIVEN( "A case-insensitive plain-text matcher" )
    {
        auto matcher = makePlainMatcher( QStringLiteral( "LINE" ), Qt::CaseInsensitive );
        THEN( "it matches regardless of case, at columns 31..34" )
        {
            REQUIRE( matcher.isLineMatchingBackward( kLine ) );
            auto [ start, end ] = matcher.getLastMatch();
            REQUIRE( start == 31_lcol );
            REQUIRE( end == 34_lcol );
        }
    }

    GIVEN( "A line with thousands of occurrences (pathological for globalMatch)" )
    {
        const QString dense( 10000, QLatin1Char( 'a' ) );
        auto matcher = makePlainMatcher( QStringLiteral( "a" ) );
        THEN( "the last occurrence is found without enumerating them all" )
        {
            REQUIRE( matcher.isLineMatchingBackward( dense ) );
            auto [ start, end ] = matcher.getLastMatch();
            REQUIRE( start == 9999_lcol );
            REQUIRE( end == 9999_lcol );
        }
    }
}
