/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

// Behaviour of the Qt-free PCRE2 wrapper: literal expectations only, so this
// suite runs without Qt. The differential comparison against QRegularExpression
// lives in qt_parity_test.cpp, which is only compiled where Qt is available.

#include <catch2/catch.hpp>

#include "pcre2regex.h"

#include <cstddef>
#include <string>
#include <vector>

namespace {

using klogg::Pcre2Regex;
using klogg::Pcre2RegexMatch;
using klogg::Pcre2RegexMatchIterator;

std::string text( std::string_view view )
{
    return std::string( view );
}

std::vector<std::string> collectMatches( Pcre2Regex& regex, std::string_view subject )
{
    std::vector<std::string> matches;
    auto iterator = regex.globalMatch( subject );
    while ( iterator.hasNext() ) {
        matches.push_back( text( *iterator.next().captured( 0 ) ) );
    }
    return matches;
}

} // namespace

SCENARIO( "Pcre2Regex pattern validation", "[pcre2]" )
{
    WHEN( "The pattern compiles" )
    {
        const Pcre2Regex regex( "WARN" );
        REQUIRE( regex.isValid() );
        // Mirrors QRegularExpression::errorString(), which reports "no error"
        // rather than an empty string on success
        REQUIRE( regex.errorString() == "no error" );
        REQUIRE( regex.pattern() == "WARN" );
    }

    WHEN( "The pattern does not compile" )
    {
        const Pcre2Regex regex( "a(b" );
        REQUIRE_FALSE( regex.isValid() );
        REQUIRE_FALSE( regex.errorString().empty() );
    }
}

SCENARIO( "Pcre2Regex byte offsets and captures", "[pcre2]" )
{
    const std::string subject = "INFO alpha\nWARN beta";

    WHEN( "The pattern matches" )
    {
        const Pcre2Regex regex( "WARN" );
        const auto match = regex.match( subject );

        REQUIRE( match.isValid() );
        REQUIRE( match.hasMatch() );
        REQUIRE( match.lastCapturedIndex() == 0 );
        REQUIRE( match.captured( 0 ).has_value() );
        REQUIRE( text( *match.captured( 0 ) ) == "WARN" );
        // Offsets are UTF-8 byte offsets into the subject
        REQUIRE( match.capturedStart( 0 ) == 11 );
        REQUIRE( match.capturedEnd( 0 ) == 15 );
        REQUIRE( match.capturedLength( 0 ) == 4 );
    }

    WHEN( "The pattern does not match" )
    {
        const Pcre2Regex regex( "ERROR" );
        const auto match = regex.match( subject );

        REQUIRE( match.isValid() );
        REQUIRE_FALSE( match.hasMatch() );
        REQUIRE_FALSE( match.captured( 0 ).has_value() );
        REQUIRE( match.capturedStart( 0 ) == -1 );
        REQUIRE( match.capturedEnd( 0 ) == -1 );
    }

    WHEN( "Numbered groups capture" )
    {
        const Pcre2Regex regex( R"((\w+)@(\w+)\.com)" );
        const auto match = regex.match( "mail: bob@example.com!" );

        REQUIRE( match.hasMatch() );
        REQUIRE( match.lastCapturedIndex() == 2 );

        REQUIRE( text( *match.captured( 0 ) ) == "bob@example.com" );
        REQUIRE( match.capturedStart( 0 ) == 6 );
        REQUIRE( match.capturedEnd( 0 ) == 21 );

        REQUIRE( text( *match.captured( 1 ) ) == "bob" );
        REQUIRE( match.capturedStart( 1 ) == 6 );
        REQUIRE( match.capturedEnd( 1 ) == 9 );

        REQUIRE( text( *match.captured( 2 ) ) == "example" );
        REQUIRE( match.capturedStart( 2 ) == 10 );
        REQUIRE( match.capturedEnd( 2 ) == 17 );
    }

    WHEN( "An optional group stays unset" )
    {
        const Pcre2Regex regex( "a(b)?c" );
        const auto match = regex.match( "ac" );

        REQUIRE( match.hasMatch() );
        REQUIRE( text( *match.captured( 0 ) ) == "ac" );
        REQUIRE_FALSE( match.hasCaptured( 1 ) );
        REQUIRE_FALSE( match.captured( 1 ).has_value() );
        REQUIRE( match.capturedStart( 1 ) == -1 );
    }

    WHEN( "Named groups are addressable" )
    {
        const Pcre2Regex regex( R"((?<user>\w+)@(?<host>\w+))" );
        REQUIRE( regex.namedCaptureGroups() == std::vector<std::string>{ "", "user", "host" } );
        REQUIRE( regex.captureIndexForName( "user" ) == 1 );
        REQUIRE( regex.captureIndexForName( "host" ) == 2 );
        REQUIRE( regex.captureIndexForName( "missing" ) == -1 );

        const auto match = regex.match( "bob@example" );
        REQUIRE( match.hasCaptured( "host" ) );
        REQUIRE( text( *match.captured( "host" ) ) == "example" );
        REQUIRE( match.capturedStart( "host" ) == 4 );
        REQUIRE( match.capturedLength( "host" ) == 7 );
    }
}

SCENARIO( "Pcre2Regex multi-byte subjects", "[pcre2]" )
{
    WHEN( "The match starts after a multi-byte character" )
    {
        const Pcre2Regex regex( "é" );
        const auto match = regex.match( "aéb" );

        REQUIRE( match.hasMatch() );
        REQUIRE( match.capturedStart( 0 ) == 1 );
        REQUIRE( match.capturedLength( 0 ) == 2 );
    }

    WHEN( "Unicode properties are enabled explicitly" )
    {
        const Pcre2Regex asciiWord( R"(\w+)" );
        const Pcre2Regex unicodeWord( R"(\w+)", Pcre2Regex::UseUnicodePropertiesOption );

        REQUIRE( asciiWord.match( "café" ).capturedLength( 0 ) == 3 );
        REQUIRE( unicodeWord.match( "café" ).capturedLength( 0 ) == 5 );
    }
}

SCENARIO( "Pcre2Regex pattern options", "[pcre2]" )
{
    WHEN( "Matching case insensitively" )
    {
        const Pcre2Regex sensitive( "warn" );
        const Pcre2Regex insensitive( "warn", Pcre2Regex::CaseInsensitiveOption );

        REQUIRE_FALSE( sensitive.match( "WARN beta" ).hasMatch() );
        REQUIRE( insensitive.match( "WARN beta" ).hasMatch() );
    }

    WHEN( "Letting dot match newlines" )
    {
        const Pcre2Regex dotAll( "a.b", Pcre2Regex::DotMatchesEverythingOption );

        REQUIRE_FALSE( Pcre2Regex( "a.b" ).match( "a\nb" ).hasMatch() );
        REQUIRE( dotAll.match( "a\nb" ).hasMatch() );
    }

    WHEN( "Anchoring per line" )
    {
        const Pcre2Regex multiline( "^WARN", Pcre2Regex::MultilineOption );

        REQUIRE_FALSE( Pcre2Regex( "^WARN" ).match( "INFO\nWARN beta" ).hasMatch() );
        REQUIRE( multiline.match( "INFO\nWARN beta" ).hasMatch() );
        REQUIRE( multiline.match( "INFO\nWARN beta" ).capturedStart( 0 ) == 5 );
    }

    WHEN( "Not capturing groups" )
    {
        const Pcre2Regex noCapture( "(a)(b)", Pcre2Regex::DontCaptureOption );
        const auto match = noCapture.match( "ab" );

        REQUIRE( match.hasMatch() );
        REQUIRE( match.lastCapturedIndex() == 0 );
        REQUIRE( text( *match.captured( 0 ) ) == "ab" );
    }

    WHEN( "Option state is readable and writable" )
    {
        Pcre2Regex regex( "warn" );
        REQUIRE( regex.patternOptions() == Pcre2Regex::NoPatternOption );

        regex.setPatternOptions( Pcre2Regex::CaseInsensitiveOption );
        REQUIRE( regex.patternOptions() == Pcre2Regex::CaseInsensitiveOption );
        REQUIRE( regex.match( "WARN" ).hasMatch() );
    }
}

SCENARIO( "Pcre2Regex iteration", "[pcre2]" )
{
    WHEN( "Iterating over global matches" )
    {
        Pcre2Regex regex( R"(\d+)" );
        REQUIRE( collectMatches( regex, "a1b22c333" )
                 == std::vector<std::string>{ "1", "22", "333" } );
    }

    WHEN( "Iterating from an offset" )
    {
        Pcre2Regex regex( R"(\d+)" );
        auto iterator = regex.globalMatch( "a1b22c333", 3 );

        REQUIRE( iterator.hasNext() );
        REQUIRE( text( *iterator.next().captured( 0 ) ) == "22" );
        REQUIRE( iterator.hasNext() );
        REQUIRE( text( *iterator.next().captured( 0 ) ) == "333" );
        REQUIRE_FALSE( iterator.hasNext() );
    }

    WHEN( "There is nothing to iterate over" )
    {
        Pcre2Regex regex( R"(\d+)" );
        auto iterator = regex.globalMatch( "abc" );
        REQUIRE_FALSE( iterator.hasNext() );
    }
}

SCENARIO( "Pcre2Regex escaping helpers", "[pcre2]" )
{
    WHEN( "Escaping metacharacters for a literal search" )
    {
        const auto escaped = Pcre2Regex::escape( "a.b*c" );
        const Pcre2Regex regex( escaped );

        REQUIRE( regex.isValid() );
        REQUIRE( regex.match( "a.b*c" ).hasMatch() );
        REQUIRE_FALSE( regex.match( "axbxc" ).hasMatch() );
    }

    WHEN( "Building a regex from a wildcard" )
    {
        const auto regex = Pcre2Regex::fromWildcard( "*.log" );
        REQUIRE( regex.isValid() );
        REQUIRE( regex.match( "test.log" ).hasMatch() );
        REQUIRE_FALSE( regex.match( "test.txt" ).hasMatch() );
    }
}
