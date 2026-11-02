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

#include "pcre2regex.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace klogg {

namespace {

struct JitStackDeleter {
    void operator()( pcre2_jit_stack* stack ) const
    {
        pcre2_jit_stack_free( stack );
    }
};

// A JIT stack per thread, lazily grown from 32K to 512K on PCRE2_ERROR_JIT_STACKLIMIT,
// mirroring QRegularExpression's safe_pcre2_match().
thread_local std::unique_ptr<pcre2_jit_stack, JitStackDeleter> threadJitStack;

pcre2_jit_stack* jitStackCallback( void* )
{
    return threadJitStack.get();
}

bool isContinuationByte( char c )
{
    return ( static_cast<unsigned char>( c ) & 0xC0U ) == 0x80U;
}

int safePcre2Match( const pcre2_code* code, PCRE2_SPTR subject, size_t subjectLength,
                    PCRE2_SIZE startOffset, uint32_t options, pcre2_match_data* matchData,
                    pcre2_match_context* matchContext )
{
    int result = pcre2_match( code, subject, subjectLength, startOffset, options, matchData, matchContext );

    if ( result == PCRE2_ERROR_JIT_STACKLIMIT && !threadJitStack ) {
        threadJitStack.reset( pcre2_jit_stack_create( 32 * 1024, 512 * 1024, nullptr ) );
        result = pcre2_match( code, subject, subjectLength, startOffset, options, matchData, matchContext );
    }

    return result;
}

struct MatchResult {
    std::vector<int64_t> offsets; // pair (start, end) per group, -1 == unset
    int capturedCount = 0;
    bool isValid = false;
    bool hasMatch = false;
};

MatchResult runMatch( const pcre2_code* code, std::string_view subject, int64_t offset,
                      bool usingCrLfNewlines, const MatchResult* previous )
{
    const auto subjectLength = static_cast<size_t>( subject.size() );

    // PCRE2 does not accept a null subject pointer, even for zero-length subjects.
    const char dummySubject = '\0';
    const auto* subjectPtr = subject.empty() ? &dummySubject : subject.data();

    uint32_t options = 0;
    const bool previousMatchWasEmpty = previous != nullptr && previous->hasMatch
        && previous->offsets.size() >= 2 && previous->offsets[0] == previous->offsets[1];

    pcre2_match_context* matchContext = pcre2_match_context_create( nullptr );
    pcre2_match_data* matchData = pcre2_match_data_create_from_pattern( code, nullptr );

    MatchResult result;

    if ( matchContext != nullptr && matchData != nullptr ) {
        pcre2_jit_stack_assign( matchContext, &jitStackCallback, nullptr );

        int rc;
        if ( !previousMatchWasEmpty ) {
            rc = safePcre2Match( code, PCRE2_SPTR( subjectPtr ), subjectLength,
                                 static_cast<PCRE2_SIZE>( offset ), options, matchData, matchContext );
        } else {
            rc = safePcre2Match( code, PCRE2_SPTR( subjectPtr ), subjectLength,
                                 static_cast<PCRE2_SIZE>( offset ),
                                 options | PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED, matchData,
                                 matchContext );

            if ( rc == PCRE2_ERROR_NOMATCH ) {
                ++offset;

                if ( usingCrLfNewlines && offset < static_cast<int64_t>( subjectLength )
                     && subject[static_cast<size_t>( offset - 1 )] == '\r'
                     && subject[static_cast<size_t>( offset )] == '\n' ) {
                    ++offset;
                } else if ( offset < static_cast<int64_t>( subjectLength )
                            && isContinuationByte( subject[static_cast<size_t>( offset )] ) ) {
                    // Landed inside a multi-byte UTF-8 sequence: advance to the next
                    // code point boundary, mirroring the UTF-16 low-surrogate skip.
                    while ( offset < static_cast<int64_t>( subjectLength )
                            && isContinuationByte( subject[static_cast<size_t>( offset )] ) ) {
                        ++offset;
                    }
                }

                rc = safePcre2Match( code, PCRE2_SPTR( subjectPtr ), subjectLength,
                                     static_cast<PCRE2_SIZE>( offset ), options, matchData,
                                     matchContext );
            }
        }

        if ( rc > 0 ) {
            result.isValid = true;
            result.hasMatch = true;
            result.capturedCount = rc;
            result.offsets.resize( static_cast<size_t>( rc ) * 2 );

            const auto* ovector = pcre2_get_ovector_pointer( matchData );
            for ( int i = 0; i < rc * 2; ++i ) {
                result.offsets[static_cast<size_t>( i )]
                    = ovector[i] == PCRE2_UNSET ? -1 : static_cast<int64_t>( ovector[i] );
            }
        } else if ( rc == PCRE2_ERROR_NOMATCH || rc == PCRE2_ERROR_PARTIAL ) {
            result.isValid = true;
        }
    }

    pcre2_match_data_free( matchData );
    pcre2_match_context_free( matchContext );

    return result;
}

bool isAlnumAscii( char c )
{
    return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' );
}

char32_t decodeUtf8( std::string_view str, size_t index, size_t* sequenceLength )
{
    const auto c = static_cast<unsigned char>( str[index] );
    size_t length = 1;
    if ( ( c & 0xE0U ) == 0xC0U ) {
        length = 2;
    } else if ( ( c & 0xF0U ) == 0xE0U ) {
        length = 3;
    } else if ( ( c & 0xF8U ) == 0xF0U ) {
        length = 4;
    }

    if ( index + length > str.size() ) {
        *sequenceLength = 1;
        return static_cast<char32_t>( -1 );
    }

    for ( size_t i = 1; i < length; ++i ) {
        if ( !isContinuationByte( str[index + i] ) ) {
            *sequenceLength = 1;
            return static_cast<char32_t>( -1 );
        }
    }

    *sequenceLength = length;

    char32_t codepoint = 0;
    switch ( length ) {
    case 2:
        codepoint = c & 0x1FU;
        break;
    case 3:
        codepoint = c & 0x0FU;
        break;
    case 4:
        codepoint = c & 0x07U;
        break;
    default:
        codepoint = c;
        break;
    }

    for ( size_t i = 1; i < length; ++i ) {
        codepoint = ( codepoint << 6 ) | ( static_cast<unsigned char>( str[index + i] ) & 0x3FU );
    }

    // Reject overlong encodings and out-of-range values.
    if ( ( length == 2 && codepoint < 0x80 ) || ( length == 3 && codepoint < 0x800 )
         || ( length == 4 && codepoint < 0x10000 ) || codepoint > 0x10FFFF ) {
        *sequenceLength = 1;
        return static_cast<char32_t>( -1 );
    }

    return codepoint;
}

std::string wildcardToRegularExpression( std::string_view pattern, bool pathMode )
{
    std::string rx;
    rx.reserve( pattern.size() + pattern.size() / 16 );

    char nativePathSeparator = '/';
#ifdef _WIN32
    nativePathSeparator = '\\';
#endif

    const std::string starEscape
        = pathMode
#ifdef _WIN32
            ? "[^/\\\\]*"
#else
            ? "[^/]*"
#endif
            : "[\\d\\D]*";
    const std::string questionMarkEscape
        = pathMode
#ifdef _WIN32
            ? "[^/\\\\]"
#else
            ? "[^/]"
#endif
            : "[\\d\\D]";

    size_t i = 0;
    while ( i < pattern.size() ) {
        const char c = pattern[i++];
        switch ( c ) {
        case '*':
            rx += starEscape;
            break;
        case '?':
            rx += questionMarkEscape;
            break;
#ifdef _WIN32
        case '\\':
            rx += pathMode ? "[/\\\\]" : "\\\\";
            break;
        case '/':
            rx += pathMode ? "[/\\\\]" : "/";
            break;
#endif
        case '$':
        case '(':
        case ')':
        case '+':
        case '.':
        case '^':
        case '{':
        case '|':
        case '}':
            rx += '\\';
            rx += c;
            break;
        case '[':
            rx += c;
            // Support for the [!abc] or [!a-c] syntax
            if ( i < pattern.size() ) {
                if ( pattern[i] == '!' ) {
                    rx += '^';
                    ++i;
                }

                if ( i < pattern.size() && pattern[i] == ']' ) {
                    rx += pattern[i++];
                }

                while ( i < pattern.size() && pattern[i] != ']' ) {
                    if ( pathMode && ( pattern[i] == '/' || pattern[i] == nativePathSeparator ) ) {
                        // A path separator inside a character class would break the regex;
                        // QRegularExpression leaves the pattern unanchored in this case.
                        return rx;
                    }
                    if ( pattern[i] == '\\' ) {
                        rx += '\\';
                    }
                    rx += pattern[i++];
                }
            }
            break;
        default:
            rx += c;
            break;
        }
    }

    rx = "\\A(?:" + rx + ")\\z";
    return rx;
}

} // namespace

struct Pcre2Regex::Impl {
    std::string pattern;
    PatternOptions options = NoPatternOption;
    pcre2_code* compiled = nullptr;
    int errorCode = 0; // pcre2_compile leaves 100 ("no error") on success, not zero;
                       // validity must be tested via compiled
    std::string errorMessage;
    uint32_t capturingCount = 0;
    bool usingCrLfNewlines = false;
    std::vector<std::pair<int, std::string>> namedCaptures;

    ~Impl()
    {
        if ( compiled != nullptr ) {
            pcre2_code_free( compiled );
        }
    }

    void compile()
    {
        if ( compiled != nullptr ) {
            pcre2_code_free( compiled );
            compiled = nullptr;
        }

        errorCode = 0;
        errorMessage.clear();
        capturingCount = 0;
        usingCrLfNewlines = false;
        namedCaptures.clear();

        // pcre2 options are uint32_t; keeping the accumulator in that type
        // avoids the int/unsigned conversions GCC reports as sign changes
        uint32_t pcreOptions = PCRE2_UTF;
        if ( options & CaseInsensitiveOption ) {
            pcreOptions |= PCRE2_CASELESS;
        }
        if ( options & DotMatchesEverythingOption ) {
            pcreOptions |= PCRE2_DOTALL;
        }
        if ( options & MultilineOption ) {
            pcreOptions |= PCRE2_MULTILINE;
        }
        if ( options & ExtendedPatternSyntaxOption ) {
            pcreOptions |= PCRE2_EXTENDED;
        }
        if ( options & InvertedGreedinessOption ) {
            pcreOptions |= PCRE2_UNGREEDY;
        }
        if ( options & DontCaptureOption ) {
            pcreOptions |= PCRE2_NO_AUTO_CAPTURE;
        }
        if ( options & UseUnicodePropertiesOption ) {
            pcreOptions |= PCRE2_UCP;
        }

        const char dummyPattern = '\0';
        const auto* patternPtr = pattern.empty() ? &dummyPattern : pattern.data();

        PCRE2_SIZE errorOffset = 0;
        compiled = pcre2_compile( PCRE2_SPTR( patternPtr ), pattern.size(), pcreOptions, &errorCode,
                                  &errorOffset, nullptr );

        if ( compiled == nullptr ) {
            std::array<PCRE2_UCHAR, 256> buffer{};
            const int length = pcre2_get_error_message( errorCode, buffer.data(), buffer.size() );
            if ( length > 0 ) {
                errorMessage.assign( reinterpret_cast<const char*>( buffer.data() ),
                                     static_cast<size_t>( length ) );
            } else {
                errorMessage = "PCRE2 error ";
                errorMessage += std::to_string( errorCode );
            }
            return;
        }

        // JIT failures are silent; the interpreter still works.
        pcre2_jit_compile( compiled,
                           PCRE2_JIT_COMPLETE | PCRE2_JIT_PARTIAL_SOFT | PCRE2_JIT_PARTIAL_HARD );

        pcre2_pattern_info( compiled, PCRE2_INFO_CAPTURECOUNT, &capturingCount );

        PCRE2_SPTR nameTable = nullptr;
        uint32_t nameCount = 0;
        uint32_t nameEntrySize = 0;
        pcre2_pattern_info( compiled, PCRE2_INFO_NAMETABLE, &nameTable );
        pcre2_pattern_info( compiled, PCRE2_INFO_NAMECOUNT, &nameCount );
        pcre2_pattern_info( compiled, PCRE2_INFO_NAMEENTRYSIZE, &nameEntrySize );

        for ( uint32_t i = 0; i < nameCount; ++i ) {
            const auto* row = nameTable + i * nameEntrySize;
            const int index = ( static_cast<int>( row[0] ) << 8 ) | static_cast<int>( row[1] );
            const auto* name = reinterpret_cast<const char*>( row + 2 );
            namedCaptures.emplace_back( index, std::string( name ) );
        }

        uint32_t newlineSetting = 0;
        if ( pcre2_pattern_info( compiled, PCRE2_INFO_NEWLINE, &newlineSetting ) != 0 ) {
            pcre2_config( PCRE2_CONFIG_NEWLINE, &newlineSetting );
        }

        usingCrLfNewlines = newlineSetting == PCRE2_NEWLINE_CRLF || newlineSetting == PCRE2_NEWLINE_ANY
            || newlineSetting == PCRE2_NEWLINE_ANYCRLF;
    }
};

Pcre2RegexMatch::Pcre2RegexMatch( const Pcre2Regex* regexp, std::shared_ptr<const std::string> subject,
                                  std::vector<int64_t> offsets, int capturedCount, bool isValid,
                                  bool hasMatch )
    : regexp_( regexp )
    , subjectHolder_( std::move( subject ) )
    , offsets_( std::move( offsets ) )
    , capturedCount_( capturedCount )
    , isValid_( isValid )
    , hasMatch_( hasMatch )
{
}

bool Pcre2RegexMatch::isValid() const
{
    return isValid_;
}

bool Pcre2RegexMatch::hasMatch() const
{
    return hasMatch_;
}

int Pcre2RegexMatch::lastCapturedIndex() const
{
    return capturedCount_ - 1;
}

bool Pcre2RegexMatch::hasCaptured( int nth ) const
{
    if ( nth < 0 || nth > lastCapturedIndex() ) {
        return false;
    }

    return offsets_[static_cast<size_t>( nth ) * 2] != -1;
}

bool Pcre2RegexMatch::hasCaptured( std::string_view name ) const
{
    return captured( name ).has_value();
}

std::optional<std::string_view> Pcre2RegexMatch::captured( int nth ) const
{
    if ( !hasCaptured( nth ) ) {
        return std::nullopt;
    }

    const auto start = static_cast<size_t>( offsets_[static_cast<size_t>( nth ) * 2] );
    const auto end = static_cast<size_t>( offsets_[static_cast<size_t>( nth ) * 2 + 1] );

    if ( subjectHolder_ == nullptr ) {
        return std::nullopt;
    }

    return std::string_view( *subjectHolder_ ).substr( start, end - start );
}

std::optional<std::string_view> Pcre2RegexMatch::captured( std::string_view name ) const
{
    if ( name.empty() || regexp_ == nullptr ) {
        return std::nullopt;
    }

    const int nth = regexp_->captureIndexForName( name );
    if ( nth == -1 ) {
        return std::nullopt;
    }

    return captured( nth );
}

int64_t Pcre2RegexMatch::capturedStart( int nth ) const
{
    if ( !hasCaptured( nth ) ) {
        return -1;
    }

    return offsets_[static_cast<size_t>( nth ) * 2];
}

int64_t Pcre2RegexMatch::capturedEnd( int nth ) const
{
    if ( !hasCaptured( nth ) ) {
        return -1;
    }

    return offsets_[static_cast<size_t>( nth ) * 2 + 1];
}

int64_t Pcre2RegexMatch::capturedLength( int nth ) const
{
    return capturedEnd( nth ) - capturedStart( nth );
}

int64_t Pcre2RegexMatch::capturedStart( std::string_view name ) const
{
    return regexp_ != nullptr ? capturedStart( regexp_->captureIndexForName( name ) ) : -1;
}

int64_t Pcre2RegexMatch::capturedEnd( std::string_view name ) const
{
    return regexp_ != nullptr ? capturedEnd( regexp_->captureIndexForName( name ) ) : -1;
}

int64_t Pcre2RegexMatch::capturedLength( std::string_view name ) const
{
    return regexp_ != nullptr ? capturedLength( regexp_->captureIndexForName( name ) ) : -1;
}

Pcre2RegexMatchIterator::Pcre2RegexMatchIterator( const Pcre2Regex* regexp,
                                                  std::shared_ptr<const std::string> subject )
    : regexp_( regexp )
    , subjectHolder_( std::move( subject ) )
{
}

bool Pcre2RegexMatchIterator::hasNext() const
{
    return next_.isValid() && next_.hasMatch();
}

Pcre2RegexMatch Pcre2RegexMatchIterator::next()
{
    if ( !hasNext() ) {
        return next_;
    }

    const Pcre2RegexMatch current = next_;

    MatchResult previousResult;
    previousResult.hasMatch = next_.hasMatch_;
    previousResult.capturedCount = next_.capturedCount_;
    previousResult.offsets = next_.offsets_;

    const auto* impl = regexp_ != nullptr ? regexp_->impl_.get() : nullptr;
    if ( impl != nullptr && impl->compiled != nullptr && !next_.offsets_.empty() ) {
        const MatchResult matchResult = runMatch( impl->compiled, *subjectHolder_,
                                                  next_.offsets_[1], impl->usingCrLfNewlines,
                                                  &previousResult );
        next_.regexp_ = regexp_;
        next_.subjectHolder_ = subjectHolder_;
        next_.offsets_ = matchResult.offsets;
        next_.capturedCount_ = matchResult.capturedCount;
        next_.isValid_ = matchResult.isValid;
        next_.hasMatch_ = matchResult.hasMatch;
    } else {
        next_ = Pcre2RegexMatch{};
    }

    return current;
}

const Pcre2RegexMatch& Pcre2RegexMatchIterator::peekNext() const
{
    return next_;
}

Pcre2Regex::Pcre2Regex( std::string pattern, PatternOptions options )
    : impl_( std::make_unique<Impl>() )
{
    impl_->pattern = std::move( pattern );
    impl_->options = options;
    impl_->compile();
}

Pcre2Regex::Pcre2Regex() = default;

Pcre2Regex::~Pcre2Regex() = default;

Pcre2Regex::Pcre2Regex( Pcre2Regex&& ) noexcept = default;
Pcre2Regex& Pcre2Regex::operator=( Pcre2Regex&& ) noexcept = default;

Pcre2RegexMatch Pcre2Regex::match( std::string_view subject, int64_t offset ) const
{
    if ( impl_ == nullptr || impl_->compiled == nullptr ) {
        return Pcre2RegexMatch{};
    }

    const auto subjectLength = static_cast<int64_t>( subject.size() );

    if ( offset < 0 ) {
        offset += subjectLength;
    }

    if ( offset < 0 || offset > subjectLength ) {
        return Pcre2RegexMatch{};
    }

    const MatchResult result = runMatch( impl_->compiled, subject, offset, impl_->usingCrLfNewlines,
                                         nullptr );

    return Pcre2RegexMatch( this, std::make_shared<std::string>( subject ), result.offsets,
                            result.capturedCount, result.isValid, result.hasMatch );
}

Pcre2RegexMatchIterator Pcre2Regex::globalMatch( std::string_view subject, int64_t offset ) const
{
    auto subjectHolder = std::make_shared<const std::string>( subject );
    Pcre2RegexMatchIterator iterator( this, subjectHolder );
    iterator.next_ = match( *subjectHolder, offset );
    return iterator;
}

bool Pcre2Regex::isValid() const
{
    return impl_ != nullptr && impl_->compiled != nullptr;
}

std::string Pcre2Regex::errorString() const
{
    if ( impl_ == nullptr || impl_->compiled != nullptr ) {
        return "no error";
    }

    return impl_->errorMessage;
}

std::string Pcre2Regex::pattern() const
{
    return impl_ != nullptr ? impl_->pattern : std::string();
}

Pcre2Regex::PatternOptions Pcre2Regex::patternOptions() const
{
    return impl_ != nullptr ? impl_->options : PatternOptions{ NoPatternOption };
}

void Pcre2Regex::setPatternOptions( PatternOptions options )
{
    if ( impl_ == nullptr ) {
        impl_ = std::make_unique<Impl>();
    }

    if ( impl_->options == options ) {
        return;
    }

    impl_->options = options;
    impl_->compile();
}

std::vector<std::string> Pcre2Regex::namedCaptureGroups() const
{
    if ( !isValid() ) {
        return {};
    }

    std::vector<std::string> result( static_cast<size_t>( impl_->capturingCount ) + 1 );
    for ( const auto& namedCapture : impl_->namedCaptures ) {
        result[static_cast<size_t>( namedCapture.first )] = namedCapture.second;
    }

    return result;
}

int Pcre2Regex::captureIndexForName( std::string_view name ) const
{
    if ( !isValid() ) {
        return -1;
    }

    for ( const auto& namedCapture : impl_->namedCaptures ) {
        if ( namedCapture.second == name ) {
            return namedCapture.first;
        }
    }

    return -1;
}

std::string Pcre2Regex::escape( std::string_view str )
{
    std::string result;
    result.reserve( str.size() * 2 );

    size_t i = 0;
    while ( i < str.size() ) {
        const auto c = static_cast<unsigned char>( str[i] );

        if ( c == 0 ) {
            // Unlike Perl, a literal NUL is escaped with "\\0", since PCRE2
            // compiles NUL-terminated strings.
            result += '\\';
            result += '0';
            ++i;
            continue;
        }

        if ( c < 0x80 ) {
            // Everything but [a-zA-Z0-9_] gets escaped, cf. perldoc -f quotemeta
            if ( isAlnumAscii( static_cast<char>( c ) ) || c == '_' ) {
                result += static_cast<char>( c );
            } else {
                result += '\\';
                result += static_cast<char>( c );
            }
            ++i;
            continue;
        }

        size_t sequenceLength = 1;
        const char32_t codepoint = decodeUtf8( str, i, &sequenceLength );

        if ( codepoint == static_cast<char32_t>( -1 ) ) {
            result += '\\';
            result += static_cast<char>( c );
            ++i;
            continue;
        }

        // A single backslash precedes the whole code point, mirroring the
        // backslash + surrogate pair form of QRegularExpression::escape.
        result += '\\';
        result.append( str.substr( i, sequenceLength ) );
        i += sequenceLength;
    }

    return result;
}

Pcre2Regex Pcre2Regex::fromWildcard( std::string_view pattern, bool caseSensitive )
{
    const std::string rx = wildcardToRegularExpression( pattern, true );
    return Pcre2Regex( rx, caseSensitive ? NoPatternOption : CaseInsensitiveOption );
}

} // namespace klogg
