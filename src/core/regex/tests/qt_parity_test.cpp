/*
 * Copyright (C) 2025 Anton Filimonov and other contributors
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

// Differential tests: the same pattern/options/subject are fed to
// QRegularExpression and to klogg::Pcre2Regex, and the results are compared.

#include <catch2/catch.hpp>

#include <QRegularExpression>

#include "pcre2regex.h"

namespace {

using klogg::Pcre2Regex;
using klogg::Pcre2RegexMatch;
using klogg::Pcre2RegexMatchIterator;

std::string toStdString( std::string_view view )
{
    return std::string( view );
}

QString toQString( std::string_view text )
{
    return QString::fromUtf8( text.data(), static_cast<int>( text.size() ) );
}

// Pcre2Regex offsets are UTF-8 byte offsets, QRegularExpression offsets are
// UTF-16 code unit offsets. For valid UTF-8 subjects this conversion is exact.
int toUtf16Offset( const std::string& subject, int64_t byteOffset )
{
    int units = 0;
    size_t i = 0;
    while ( i < static_cast<size_t>( byteOffset ) ) {
        const auto c = static_cast<unsigned char>( subject[i] );
        if ( ( c & 0x80U ) == 0 ) {
            i += 1;
            ++units;
        } else if ( ( c & 0xE0U ) == 0xC0U ) {
            i += 2;
            ++units;
        } else if ( ( c & 0xF0U ) == 0xE0U ) {
            i += 3;
            ++units;
        } else {
            // 4-byte sequences occupy two UTF-16 code units (a surrogate pair)
            i += 4;
            units += 2;
        }
    }
    return units;
}

struct EnginePair {
    QRegularExpression qt;
    Pcre2Regex pcre;
};

EnginePair makePair( std::string pattern, unsigned options )
{
    return { QRegularExpression( toQString( pattern ),
                                 static_cast<QRegularExpression::PatternOptions>( options ) ),
             Pcre2Regex( std::move( pattern ), options ) };
}

void requireMatchParity( const QRegularExpressionMatch& qtMatch, const Pcre2RegexMatch& pcreMatch,
                         const std::string& subject )
{
    REQUIRE( pcreMatch.isValid() == qtMatch.isValid() );

    if ( !qtMatch.isValid() ) {
        return;
    }

    REQUIRE( pcreMatch.hasMatch() == qtMatch.hasMatch() );

    if ( !qtMatch.hasMatch() ) {
        return;
    }

    REQUIRE( pcreMatch.lastCapturedIndex() == qtMatch.lastCapturedIndex() );

    const int lastIndex = qtMatch.lastCapturedIndex();
    for ( int i = 0; i <= lastIndex; ++i ) {
        // Qt 6.2 has no hasCaptured(); an unset group reports start == -1
        REQUIRE( pcreMatch.hasCaptured( i ) == ( qtMatch.capturedStart( i ) != -1 ) );

        if ( qtMatch.capturedStart( i ) != -1 ) {
            const auto captured = pcreMatch.captured( i );
            REQUIRE( captured.has_value() );
            REQUIRE( toStdString( *captured ) == qtMatch.captured( i ).toUtf8().toStdString() );
        }

        const int64_t qtStart = qtMatch.capturedStart( i );
        const int64_t qtEnd = qtMatch.capturedEnd( i );

        if ( qtStart < 0 ) {
            REQUIRE( pcreMatch.capturedStart( i ) == -1 );
        } else {
            REQUIRE( toUtf16Offset( subject, pcreMatch.capturedStart( i ) ) == qtStart );
        }
        if ( qtEnd < 0 ) {
            REQUIRE( pcreMatch.capturedEnd( i ) == -1 );
        } else {
            REQUIRE( toUtf16Offset( subject, pcreMatch.capturedEnd( i ) ) == qtEnd );
        }

        if ( qtMatch.capturedStart( i ) != -1 ) {
            REQUIRE( toUtf16Offset( subject, pcreMatch.capturedEnd( i ) )
                         - toUtf16Offset( subject, pcreMatch.capturedStart( i ) )
                     == qtMatch.capturedLength( i ) );
        }
    }
}

void requireGlobalMatchParity( const QRegularExpression& qtRe, const Pcre2Regex& pcreRe,
                               const std::string& subject, const std::string& pattern,
                               unsigned options )
{
    QRegularExpressionMatchIterator qtIt = qtRe.globalMatch( toQString( subject ) );
    Pcre2RegexMatchIterator pcreIt = pcreRe.globalMatch( subject );

    int count = 0;
    while ( qtIt.hasNext() && pcreIt.hasNext() ) {
        const QRegularExpressionMatch qtMatch = qtIt.next();
        const Pcre2RegexMatch pcreMatch = pcreIt.next();

        requireMatchParity( qtMatch, pcreMatch, subject );

        ++count;
        REQUIRE( count < 10000 );
    }

    REQUIRE( pcreIt.hasNext() == qtIt.hasNext() );

    // A global match ending in an empty match still iterates past it.
    if ( qtIt.hasNext() ) {
        FAIL( "Pcre2Regex globalMatch stopped early for '" + pattern + "' on '" + subject + "'" );
    }

    INFO( "pattern: " << pattern << ", subject: " << subject << ", options: " << options );
    REQUIRE( count > 0 );
}

} // namespace

TEST_CASE( "Pcre2Regex matches a literal pattern identically" )
{
    const auto engines = makePair( "hello", 0 );
    const std::string subject = "say hello world";

    const auto qtMatch = engines.qt.match( toQString( subject ) );
    const auto pcreMatch = engines.pcre.match( subject );

    requireMatchParity( qtMatch, pcreMatch, subject );

    SECTION( "full match is captured as group 0" )
    {
        REQUIRE( toStdString( *pcreMatch.captured() ) == "hello" );
    }
}

TEST_CASE( "Pcre2Regex handles named capture groups identically" )
{
    const auto engines = makePair( R"((?<year>\d{4})-(?<month>\d{2})-(?<day>\d{2}))", 0 );
    const std::string subject = "date: 2024-07-15, again 1999-12-31";

    const auto qtMatch = engines.qt.match( toQString( subject ) );
    const auto pcreMatch = engines.pcre.match( subject );

    requireMatchParity( qtMatch, pcreMatch, subject );

    SECTION( "groups are accessible by name" )
    {
        REQUIRE( toStdString( *pcreMatch.captured( "year" ) ) == "2024" );
        REQUIRE( toStdString( *pcreMatch.captured( "month" ) ) == "07" );
        REQUIRE( toStdString( *pcreMatch.captured( "day" ) ) == "15" );
    }

    SECTION( "unknown and empty names yield no capture" )
    {
        REQUIRE( !pcreMatch.captured( "nope" ).has_value() );
        REQUIRE( !pcreMatch.captured( "" ).has_value() );
    }

    SECTION( "namedCaptureGroups lists names in index order" )
    {
        const auto names = engines.pcre.namedCaptureGroups();
        REQUIRE( names == std::vector<std::string>{ "", "year", "month", "day" } );
        REQUIRE( engines.pcre.captureIndexForName( "month" ) == 2 );
        REQUIRE( engines.pcre.captureIndexForName( "nope" ) == -1 );
    }
}

TEST_CASE( "Pcre2Regex handles unicode property escapes identically" )
{
    const auto engines = makePair( "\\p{Lu}\\p{Ll}*", Pcre2Regex::UseUnicodePropertiesOption );
    const std::string subject = "words: Äbc Фёдор 中文 abc Def";

    const auto qtMatch = engines.qt.match( toQString( subject ) );
    const auto pcreMatch = engines.pcre.match( subject );

    requireMatchParity( qtMatch, pcreMatch, subject );
}

TEST_CASE( "Pcre2Regex unicode case folding matches Qt" )
{
    for ( const auto& pattern : { "\\bΣ\\b", "σ+" } ) {
        const auto engines = makePair( pattern, Pcre2Regex::CaseInsensitiveOption );
        const std::string subject = "σ Σ ς ϲ";

        const auto qtMatch = engines.qt.match( toQString( subject ) );
        const auto pcreMatch = engines.pcre.match( subject );

        requireMatchParity( qtMatch, pcreMatch, subject );
    }
}

TEST_CASE( "Pcre2Regex backreference matches are identical" )
{
    const auto engines = makePair( "(a+)\\1", 0 );
    const std::string subject = "aaa aaaa aa";

    requireGlobalMatchParity( engines.qt, engines.pcre, subject, "(a+)\\1", 0 );
}

TEST_CASE( "Pcre2Regex globalMatch advances over empty matches identically" )
{
    SECTION( "repeated star on ascii" )
    {
        const auto engines = makePair( "a*", 0 );
        requireGlobalMatchParity( engines.qt, engines.pcre, "aaabbbaa", "a*", 0 );
    }

    SECTION( "empty matches inside multibyte utf-8" )
    {
        // exercises the mid-code-point advance over UTF-8 sequences
        const auto engines = makePair( "a*", 0 );
        requireGlobalMatchParity( engines.qt, engines.pcre, "a中b中", "a*", 0 );
    }

    SECTION( "empty pattern matches at every position" )
    {
        const auto engines = makePair( "", 0 );
        requireGlobalMatchParity( engines.qt, engines.pcre, "ab中", "", 0 );
    }

    SECTION( "word boundaries produce empty matches" )
    {
        const auto engines = makePair( "\\b", Pcre2Regex::UseUnicodePropertiesOption );
        requireGlobalMatchParity( engines.qt, engines.pcre, "one two 中文", "\\b",
                                  Pcre2Regex::UseUnicodePropertiesOption );
    }

    SECTION( "whitespace star across newlines" )
    {
        const auto engines = makePair( "\\s*", 0 );
        requireGlobalMatchParity( engines.qt, engines.pcre, "a b\n c\t\nd", "\\s*", 0 );
    }

    SECTION( "chinese literal repeated matches" )
    {
        const auto engines = makePair( "中+", 0 );
        requireGlobalMatchParity( engines.qt, engines.pcre, "中ab中文中", "中+", 0 );
    }
}

TEST_CASE( "Pcre2Regex invalid patterns report invalid state identically" )
{
    for ( const auto& pattern : { "(", "[a-", "*a", "\\", "a{" } ) {
        const auto engines = makePair( pattern, 0 );

        INFO( "pattern: " << pattern );
        REQUIRE( engines.pcre.isValid() == engines.qt.isValid() );
        REQUIRE( engines.pcre.errorString() == engines.qt.errorString().toUtf8().toStdString() );

        const auto qtMatch = engines.qt.match( "abc" );
        const auto pcreMatch = engines.pcre.match( "abc" );
        requireMatchParity( qtMatch, pcreMatch, "abc" );
    }
}

TEST_CASE( "Pcre2Regex pattern option mapping matches Qt" )
{
    SECTION( "case insensitive" )
    {
        const auto engines = makePair( "abc", Pcre2Regex::CaseInsensitiveOption );
        const auto qtMatch = engines.qt.match( "XYZ aBc xyz" );
        const auto pcreMatch = engines.pcre.match( "XYZ aBc xyz" );
        requireMatchParity( qtMatch, pcreMatch, "XYZ aBc xyz" );
        REQUIRE( toStdString( *pcreMatch.captured() ) == "aBc" );
    }

    SECTION( "multiline anchors" )
    {
        const auto engines = makePair( "^b", Pcre2Regex::MultilineOption );
        requireGlobalMatchParity( engines.qt, engines.pcre, "a\nb\nc\nb", "^b",
                                  Pcre2Regex::MultilineOption );
    }

    SECTION( "dot matches everything" )
    {
        const auto engines = makePair( "a.c", Pcre2Regex::DotMatchesEverythingOption );
        const auto qtMatch = engines.qt.match( "a\nc" );
        const auto pcreMatch = engines.pcre.match( "a\nc" );
        requireMatchParity( qtMatch, pcreMatch, "a\nc" );
    }

    SECTION( "inverted greediness" )
    {
        const auto engines = makePair( "a+", Pcre2Regex::InvertedGreedinessOption );
        const auto qtMatch = engines.qt.match( "aaaa" );
        const auto pcreMatch = engines.pcre.match( "aaaa" );
        requireMatchParity( qtMatch, pcreMatch, "aaaa" );
        REQUIRE( toStdString( *pcreMatch.captured() ) == "a" );
    }

    SECTION( "dont capture" )
    {
        const auto engines = makePair( "(a)(b)c", Pcre2Regex::DontCaptureOption );
        const auto qtMatch = engines.qt.match( "abc" );
        const auto pcreMatch = engines.pcre.match( "abc" );
        requireMatchParity( qtMatch, pcreMatch, "abc" );
        REQUIRE( pcreMatch.lastCapturedIndex() == 0 );
    }
}

TEST_CASE( "Pcre2Regex match offset semantics match Qt" )
{
    const auto engines = makePair( "bc", 0 );
    const std::string subject = "abcdef";

    SECTION( "positive offset skips the subject prefix" )
    {
        const auto qtMatch = engines.qt.match( toQString( subject ), 3 );
        const auto pcreMatch = engines.pcre.match( subject, 3 );
        REQUIRE( !pcreMatch.hasMatch() );
        REQUIRE( pcreMatch.isValid() == qtMatch.isValid() );
    }

    SECTION( "offset at the exact match position works" )
    {
        const auto qtMatch = engines.qt.match( toQString( subject ), 1 );
        const auto pcreMatch = engines.pcre.match( subject, 1 );
        requireMatchParity( qtMatch, pcreMatch, subject );
    }

    SECTION( "negative offset counts from the end" )
    {
        const auto qtMatch = engines.qt.match( toQString( subject ), -2 );
        const auto pcreMatch = engines.pcre.match( subject, -2 );
        REQUIRE( pcreMatch.isValid() == qtMatch.isValid() );
        REQUIRE( pcreMatch.hasMatch() == qtMatch.hasMatch() );
    }

    SECTION( "offset past the end is invalid" )
    {
        const auto qtMatch = engines.qt.match( toQString( subject ), 100 );
        const auto pcreMatch = engines.pcre.match( subject, 100 );
        REQUIRE( !pcreMatch.isValid() );
        REQUIRE( qtMatch.isValid() == pcreMatch.isValid() );
        REQUIRE( !pcreMatch.hasMatch() );
    }

    SECTION( "empty subject with offset zero" )
    {
        const auto emptyEngines = makePair( "x*", 0 );
        const auto qtMatch = emptyEngines.qt.match( "" );
        const auto pcreMatch = emptyEngines.pcre.match( "" );
        requireMatchParity( qtMatch, pcreMatch, "" );
    }
}

TEST_CASE( "Pcre2Regex escape matches Qt quotemeta" )
{
    const std::vector<std::string> inputs = { "abc",       "a.b*c",     "a+b?c(d)e",  "[x]^y{z}|p$q",
                                              "back\\slash", "中",       "中a.b文",    "x_y1a",
                                              "0\n1",      "!",         "-" };
    for ( const auto& input : inputs ) {
        INFO( "input: " << input );
        REQUIRE( Pcre2Regex::escape( input ) == QRegularExpression::escape( toQString( input ) ).toUtf8().toStdString() );
    }

    SECTION( "nul byte is escaped as backslash zero" )
    {
        const std::string input( "a\0b", 3 );
        REQUIRE( Pcre2Regex::escape( input ) == QRegularExpression::escape( toQString( input ) ).toUtf8().toStdString() );
        REQUIRE( Pcre2Regex::escape( input ) == "a\\0b" );
    }
}

TEST_CASE( "Pcre2Regex fromWildcard matches Qt glob semantics" )
{
    struct WildcardCase {
        std::string pattern;
        std::string subject;
        bool caseSensitive;
        bool shouldMatch;
    };

    const std::vector<WildcardCase> cases = {
        { "*.txt", "log.txt", true, true },
        { "*.txt", "log.txt.bak", true, false },
        { "*.txt", "logs/one/file.txt", true, false }, // path separator not matched by *
        { "logs/*.txt", "logs/inner/file.txt", true, false },
        { "a?c", "abc", true, true },
        { "a?c", "a/c", true, false },
        { "a?c", "a中c", true, true },
        { "file[0-9].log", "file7.log", true, true },
        { "file[!0-9].log", "file7.log", true, false },
        { "file[a-c].log", "fileb.log", true, true },
        { "*.TXT", "LOG.TXT", false, true },
        { "*.TXT", "LOG.TXT", true, true },
        { "prefix*", "prefix-123", true, true },
        { "a", "a", true, true },
        { "a", "aa", true, false },
    };

    for ( const auto& testCase : cases ) {
        auto qtRe = QRegularExpression::fromWildcard( toQString( testCase.pattern ),
                                                      testCase.caseSensitive ? Qt::CaseSensitive
                                                                             : Qt::CaseInsensitive );
        auto pcreRe = Pcre2Regex::fromWildcard( testCase.pattern, testCase.caseSensitive );

        INFO( "pattern: " << testCase.pattern << ", subject: " << testCase.subject );
        REQUIRE( pcreRe.isValid() == qtRe.isValid() );
        REQUIRE( pcreRe.match( testCase.subject ).hasMatch() == qtRe.match( toQString( testCase.subject ) ).hasMatch() );
        REQUIRE( pcreRe.match( testCase.subject ).hasMatch() == testCase.shouldMatch );
    }
}

TEST_CASE( "Pcre2Regex catastrophic backtracking is bounded" )
{
    auto engines = makePair( "^(a+)+$", 0 );
    const std::string subject = std::string( 30, 'a' ) + "b";

    const auto qtMatch = engines.qt.match( toQString( subject ) );
    const auto pcreMatch = engines.pcre.match( subject );

    REQUIRE( pcreMatch.isValid() == qtMatch.isValid() );
    REQUIRE( pcreMatch.hasMatch() == qtMatch.hasMatch() );
    REQUIRE( !pcreMatch.hasMatch() );

    SECTION( "pattern options are queryable" )
    {
        REQUIRE( engines.pcre.pattern() == "^(a+)+$" );
        REQUIRE( engines.pcre.patternOptions() == 0u );
    }

    SECTION( "setPatternOptions recompiles" )
    {
        engines.pcre.setPatternOptions( Pcre2Regex::CaseInsensitiveOption );
        REQUIRE( engines.pcre.patternOptions() == Pcre2Regex::CaseInsensitiveOption );
        REQUIRE( engines.pcre.match( "AAAAAAA" ).hasMatch() );
    }
}
