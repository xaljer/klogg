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

#ifndef KLOGG_PCRE2_REGEX_H
#define KLOGG_PCRE2_REGEX_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace klogg {

// Pcre2Regex is a Qt-free reimplementation of the QRegularExpression subset
// that klogg actually uses. It operates on UTF-8 strings; all offsets are
// byte offsets into the subject, whereas QRegularExpression uses UTF-16 code
// unit offsets.
class Pcre2Regex;

class Pcre2RegexMatch {
  public:
    Pcre2RegexMatch() = default;

    bool isValid() const;
    bool hasMatch() const;

    int lastCapturedIndex() const;

    bool hasCaptured( int nth ) const;
    bool hasCaptured( std::string_view name ) const;

    // Returns the captured substring, or std::nullopt if the group did not
    // capture anything or does not exist. A group that captured an empty
    // string returns an engaged optional containing an empty view.
    std::optional<std::string_view> captured( int nth = 0 ) const;
    std::optional<std::string_view> captured( std::string_view name ) const;

    int64_t capturedStart( int nth = 0 ) const;
    int64_t capturedStart( std::string_view name ) const;
    int64_t capturedEnd( int nth = 0 ) const;
    int64_t capturedEnd( std::string_view name ) const;
    int64_t capturedLength( int nth = 0 ) const;
    int64_t capturedLength( std::string_view name ) const;

  private:
    friend class Pcre2Regex;
    friend class Pcre2RegexMatchIterator;

    struct Impl;
    explicit Pcre2RegexMatch( const Pcre2Regex* regexp, std::shared_ptr<const std::string> subject,
                              std::vector<int64_t> offsets, int capturedCount, bool isValid,
                              bool hasMatch );

    const Pcre2Regex* regexp_ = nullptr;
    std::shared_ptr<const std::string> subjectHolder_;
    std::vector<int64_t> offsets_; // pair (start, end) per group, -1 == unset
    int capturedCount_ = 0;
    bool isValid_ = false;
    bool hasMatch_ = false;
};

class Pcre2RegexMatchIterator {
  public:
    Pcre2RegexMatchIterator() = default;

    bool hasNext() const;
    Pcre2RegexMatch next();
    const Pcre2RegexMatch& peekNext() const;

  private:
    friend class Pcre2Regex;
    friend class Pcre2RegexMatch;

    Pcre2RegexMatchIterator( const Pcre2Regex* regexp, std::shared_ptr<const std::string> subject );

    const Pcre2Regex* regexp_ = nullptr;
    std::shared_ptr<const std::string> subjectHolder_;
    Pcre2RegexMatch next_;
};

class Pcre2Regex {
  public:
    enum PatternOption {
        NoPatternOption = 0x0,
        CaseInsensitiveOption = 0x1,
        DotMatchesEverythingOption = 0x2,
        MultilineOption = 0x4,
        ExtendedPatternSyntaxOption = 0x8,
        InvertedGreedinessOption = 0x10,
        DontCaptureOption = 0x20,
        UseUnicodePropertiesOption = 0x40,
    };
    using PatternOptions = unsigned;

    // Default and move operations are defined out-of-line: MSVC instantiates
    // the unique_ptr<Impl> destructor when they are defaulted in-class, which
    // requires the complete Impl type.
    Pcre2Regex();
    Pcre2Regex( std::string pattern, PatternOptions options = NoPatternOption );
    ~Pcre2Regex();

    Pcre2Regex( const Pcre2Regex& ) = delete;
    Pcre2Regex& operator=( const Pcre2Regex& ) = delete;
    Pcre2Regex( Pcre2Regex&& other ) noexcept;
    Pcre2Regex& operator=( Pcre2Regex&& other ) noexcept;

    Pcre2RegexMatch match( std::string_view subject, int64_t offset = 0 ) const;
    Pcre2RegexMatchIterator globalMatch( std::string_view subject, int64_t offset = 0 ) const;

    bool isValid() const;
    std::string errorString() const;

    std::string pattern() const;
    PatternOptions patternOptions() const;
    void setPatternOptions( PatternOptions options );

    // A vector with an entry per capturing group index (group 0 is the empty
    // string). Unnamed groups have an empty entry, named groups carry their
    // name.
    std::vector<std::string> namedCaptureGroups() const;
    int captureIndexForName( std::string_view name ) const;

    static std::string escape( std::string_view str );

    // Mirrors QRegularExpression::fromWildcard with the default
    // WildcardUnicodePathMode (path separators are honored).
    static Pcre2Regex fromWildcard( std::string_view pattern, bool caseSensitive = true );

  private:
    friend class Pcre2RegexMatchIterator;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace klogg

#endif
