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

#ifndef KLOGG_REGULAR_EXPRESSION_PATTERN_H
#define KLOGG_REGULAR_EXPRESSION_PATTERN_H

#include <atomic>
#include <cstdint>
#include <string>

#include "pcre2regex.h"
#include "uuid.h"

struct RegularExpressionPattern {

    std::string pattern;
    bool isCaseSensitive = true;
    bool isExclude = false;
    bool isBoolean = false;
    bool isPlainText = false;
    bool isPrefilter = false;

    RegularExpressionPattern() = default;

    explicit RegularExpressionPattern( const std::string& expression )
        : RegularExpressionPattern( expression, true, false, false, false )
    {
    }

    RegularExpressionPattern( const std::string& expression, bool caseSensitive, bool inverse,
                              bool boolean, bool plainText )
        : pattern( expression )
        , isCaseSensitive( caseSensitive )
        , isExclude( inverse )
        , isBoolean( boolean )
        , isPlainText( plainText )
        , patternId_( nextId() )
    {
    }

    std::string id() const
    {
        return patternId_;
    }

    explicit operator klogg::Pcre2Regex() const
    {
        // Declared as PatternOptions (unsigned) rather than auto: the option
        // enumerators are plain ints, and an int passed to the unsigned
        // PatternOptions parameter is a -Wsign-conversion error on GCC.
        klogg::Pcre2Regex::PatternOptions patternOptions
            = klogg::Pcre2Regex::UseUnicodePropertiesOption;
        patternOptions |= klogg::Pcre2Regex::DontCaptureOption;

        if ( !isCaseSensitive ) {
            patternOptions |= klogg::Pcre2Regex::CaseInsensitiveOption;
        }

        auto finalPattern = pattern;
        if ( isPlainText ) {
            finalPattern = klogg::Pcre2Regex::escape( pattern );
        }

        return klogg::Pcre2Regex( finalPattern, patternOptions );
    }

    bool operator==( const RegularExpressionPattern& other ) const
    {
        return std::tie( pattern, isCaseSensitive, isExclude, isBoolean, isPlainText )
               == std::tie( other.pattern, other.isCaseSensitive, isExclude, isBoolean,
                            isPlainText );
    }

  private:
    static std::string nextId()
    {
        static std::atomic<unsigned int> counter_ = 0;
        return std::string{ "p_" } + std::to_string( counter_++ );
    }

    std::string patternId_;
};

#endif