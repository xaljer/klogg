/*
 * Copyright (C) 2009, 2010, 2011 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
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

// This file implements classes Highlighter and HighlighterSet

#include <algorithm>
#include <iterator>
#include <random>
#include <utility>

#include <simdutf.h>

#include "containers.h"
#include "crc32.h"
#include "linetypes.h"
#include "log.h"
#include "regularexpression.h"
#include "regularexpressionpattern.h"
#include "uuid.h"

#include "highlighterset.h"

namespace {

klogg::Pcre2Regex::PatternOptions getPatternOptions( bool ignoreCase )
{
    klogg::Pcre2Regex::PatternOptions options = klogg::Pcre2Regex::UseUnicodePropertiesOption;

    if ( ignoreCase ) {
        options |= klogg::Pcre2Regex::CaseInsensitiveOption;
    }
    return options;
}

// Copies QColor::darker semantics (Qt5): scales RGB towards black, keeps alpha;
// non-positive factors return the original color.
klogg::Rgba darker( const klogg::Rgba& color, int factor )
{
    if ( factor <= 0 ) {
        return color;
    }

    return klogg::Rgba(
        static_cast<uint8_t>( std::min( 255, ( color.r * 100 ) / factor ) ),
        static_cast<uint8_t>( std::min( 255, ( color.g * 100 ) / factor ) ),
        static_cast<uint8_t>( std::min( 255, ( color.b * 100 ) / factor ) ), color.a );
}

} // namespace

Highlighter::Highlighter( const std::string& pattern, bool ignoreCase, bool onlyMatch,
                          const klogg::Rgba& foreColor, const klogg::Rgba& backColor )
    : regexp_( pattern, getPatternOptions( ignoreCase ) )
    , highlightOnlyMatch_( onlyMatch )
    , color_{ foreColor, backColor }
{
    LOG_DEBUG << "New Highlighter, fore: " << color_.foreColor.toHex()
              << " back: " << color_.backColor.toHex();
}

// Pcre2Regex is not copyable, rebuild the expression from pattern + options.
Highlighter::Highlighter( const Highlighter& other )
    : regexp_( other.regexp_.pattern(), other.regexp_.patternOptions() )
    , name_( other.name_ )
    , enabled_( other.enabled_ )
    , useRegex_( other.useRegex_ )
    , highlightOnlyMatch_( other.highlightOnlyMatch_ )
    , variateColors_( other.variateColors_ )
    , colorVariance_( other.colorVariance_ )
    , color_( other.color_ )
    , useThemeBack_( other.useThemeBack_ )
{
}

Highlighter& Highlighter::operator=( const Highlighter& other )
{
    if ( this == &other ) {
        return *this;
    }

    regexp_ = klogg::Pcre2Regex( other.regexp_.pattern(), other.regexp_.patternOptions() );
    optimizedRegexp_.reset();
    captureCountCache_ = -1;

    name_ = other.name_;
    enabled_ = other.enabled_;
    useRegex_ = other.useRegex_;
    highlightOnlyMatch_ = other.highlightOnlyMatch_;
    variateColors_ = other.variateColors_;
    colorVariance_ = other.colorVariance_;
    color_ = other.color_;
    useThemeBack_ = other.useThemeBack_;

    return *this;
}

std::string Highlighter::pattern() const
{
    return regexp_.pattern();
}

void Highlighter::setPattern( const std::string& pattern )
{
    regexp_ = klogg::Pcre2Regex( pattern, regexp_.patternOptions() );
    optimizedRegexp_.reset();
    captureCountCache_ = -1;
}

bool Highlighter::ignoreCase() const
{
    return ( regexp_.patternOptions() & klogg::Pcre2Regex::CaseInsensitiveOption ) != 0;
}

void Highlighter::setIgnoreCase( bool ignoreCase )
{
    regexp_.setPatternOptions( getPatternOptions( ignoreCase ) );
    optimizedRegexp_.reset();
    captureCountCache_ = -1;
}

std::string Highlighter::name() const
{
    return name_;
}

void Highlighter::setName( const std::string& name )
{
    name_ = name;
}

bool Highlighter::isEnabled() const
{
    return enabled_;
}

void Highlighter::setEnabled( bool enabled )
{
    enabled_ = enabled;
}

bool Highlighter::useRegex() const
{
    return useRegex_;
}

void Highlighter::setUseRegex( bool useRegex )
{
    useRegex_ = useRegex;
}

bool Highlighter::highlightOnlyMatch() const
{
    return highlightOnlyMatch_;
}

void Highlighter::setHighlightOnlyMatch( bool onlyMatch )
{
    highlightOnlyMatch_ = onlyMatch;
}

bool Highlighter::variateColors() const
{
    return variateColors_;
}
void Highlighter::setVariateColors( bool variateColors )
{
    variateColors_ = variateColors;
}

int Highlighter::colorVariance() const
{
    return colorVariance_;
}

void Highlighter::setColorVariance( int colorVariance )
{
    colorVariance_ = colorVariance;
}

klogg::Rgba Highlighter::foreColor() const
{
    return color_.foreColor;
}

void Highlighter::setForeColor( const klogg::Rgba& foreColor )
{
    color_.foreColor = foreColor;
}

klogg::Rgba Highlighter::backColor() const
{
    return color_.backColor;
}

void Highlighter::setBackColor( const klogg::Rgba& backColor )
{
    color_.backColor = backColor;
}

bool Highlighter::useThemeBack() const
{
    return useThemeBack_;
}

void Highlighter::setUseThemeBack( bool useTheme )
{
    useThemeBack_ = useTheme;
}

std::pair<klogg::Rgba, klogg::Rgba> Highlighter::vairateColors( std::string_view match ) const
{
    if ( !( variateColors_ && highlightOnlyMatch_ ) ) {
        return std::make_pair( color_.foreColor, color_.backColor );
    }

    std::uniform_int_distribution<int> colorDistribution( 100 - colorVariance_,
                                                          100 + colorVariance_ );

    std::minstd_rand0 generator( Crc32::calculate( match ) );
    const auto factor = colorDistribution( generator );

    return std::make_pair( darker( color_.foreColor, factor ), darker( color_.backColor, factor ) );
}

RegularExpressionPattern Highlighter::expressionPattern() const
{
    RegularExpressionPattern result;
    result.pattern = useRegex_ ? regexp_.pattern()
                               : klogg::Pcre2Regex::escape( regexp_.pattern() );
    result.isCaseSensitive = !ignoreCase();
    result.isPrefilter = true;

    return result;
}

void Highlighter::compile() const
{
    auto pattern
        = useRegex_ ? regexp_.pattern() : klogg::Pcre2Regex::escape( regexp_.pattern() );

    optimizedRegexp_ = klogg::Pcre2Regex( std::move( pattern ), regexp_.patternOptions() );
    captureCountCache_ = -1;
}

bool Highlighter::matchLine( std::string_view line, klogg::vector<HighlightedMatch>& matches ) const
{
    matches.clear();

    if ( !enabled_ ) {
        return false;
    }

    if ( !optimizedRegexp_ ) {
        compile();
    }

    if ( captureCountCache_ < 0 ) {
        captureCountCache_
            = static_cast<int>( optimizedRegexp_->namedCaptureGroups().size() ) - 1;
    }

    auto matchIterator = optimizedRegexp_->globalMatch( line );

    while ( matchIterator.hasNext() ) {
        const auto match = matchIterator.next();
        if ( captureCountCache_ > 0 ) {
            matches.reserve( static_cast<size_t>( match.lastCapturedIndex() ) );
            for ( int i = 1; i <= match.lastCapturedIndex(); ++i ) {

                const auto colors
                    = vairateColors( match.captured( i ).value_or( std::string_view{} ) );

                const auto startByte = match.capturedStart( i );
                if ( startByte < 0 ) {
                    // A group that did not participate, historical semantics
                    matches.emplace_back( LineColumn{ -1 }, LineLength{ 0 }, colors.first,
                                          colors.second );
                }
                else {
                    const auto startU16 = simdutf::utf16_length_from_utf8(
                        line.data(), static_cast<size_t>( startByte ) );
                    const auto endU16 = simdutf::utf16_length_from_utf8(
                        line.data(), static_cast<size_t>( match.capturedEnd( i ) ) );
                    matches.emplace_back(
                        LineColumn{ static_cast<LineColumn::UnderlyingType>( startU16 ) },
                        LineLength{ static_cast<LineLength::UnderlyingType>( endU16 - startU16 ) },
                        colors.first, colors.second );
                }
            }
        }
        else {
            const auto colors
                = vairateColors( match.captured( 0 ).value_or( std::string_view{} ) );

            const auto startU16 = simdutf::utf16_length_from_utf8(
                line.data(), static_cast<size_t>( match.capturedStart( 0 ) ) );
            const auto endU16 = simdutf::utf16_length_from_utf8(
                line.data(), static_cast<size_t>( match.capturedEnd( 0 ) ) );
            matches.emplace_back(
                LineColumn{ static_cast<LineColumn::UnderlyingType>( startU16 ) },
                LineLength{ static_cast<LineLength::UnderlyingType>( endU16 - startU16 ) },
                colors.first, colors.second );
        }
    }

    return ( !matches.empty() );
}

HighlighterSet HighlighterSet::createNewSet( const std::string& name )
{
    return HighlighterSet{ name };
}

HighlighterSet::HighlighterSet( const std::string& name )
    : name_( name )
    , id_( generateIdFromUuid() )
{
}

std::string HighlighterSet::name() const
{
    return name_;
}

std::string HighlighterSet::id() const
{
    return id_;
}

void HighlighterSet::setId( const std::string& id )
{
    id_ = id;
}

bool HighlighterSet::isEmpty() const
{
    return highlighterList_.empty();
}

const klogg::vector<Highlighter>& HighlighterSet::highlighters() const
{
    return highlighterList_;
}

klogg::vector<Highlighter>& HighlighterSet::highlighters()
{
    // In-place edits invalidate the compiled expression.
    compiledExpression_.reset();
    return highlighterList_;
}

void HighlighterSet::setName( const std::string& name )
{
    name_ = name;
}

void HighlighterSet::setHighlighters( const klogg::vector<Highlighter>& highlighters )
{
    highlighterList_ = highlighters;
    compiledExpression_.reset();
}

void HighlighterSet::compile() const
{
    klogg::vector<RegularExpressionPattern> patterns( highlighterList_.size() );
    std::transform( highlighterList_.begin(), highlighterList_.end(), patterns.begin(),
                    []( const Highlighter& hl ) { return hl.expressionPattern(); } );

    compiledExpression_ = std::make_shared<MultiRegularExpression>( patterns );
}

HighlighterMatchType HighlighterSet::matchLine( std::string_view line,
                                                HighlightedMatchRanges& matches ) const
{
    if ( highlighterList_.empty() ) {
        return HighlighterMatchType::NoMatch;
    }

    if ( !compiledExpression_ ) {
        compile();
    }

    auto matcher = compiledExpression_->createMatcher();
    klogg::vector<std::pair<RegularExpressionPattern, bool>> matchedPatterns = matcher->match( line );

    auto matchType = HighlighterMatchType::NoMatch;

    for ( int index = static_cast<int>( highlighterList_.size() ) - 1; index >= 0; --index ) {
        const Highlighter& hl = highlighterList_[ static_cast<size_t>( index ) ];
        if ( !matchedPatterns[ static_cast<size_t>( index ) ].second ) {
            continue;
        }

        klogg::vector<HighlightedMatch> thisMatches;
        if ( !hl.matchLine( line, thisMatches ) ) {
            continue;
        }

        if ( hl.highlightOnlyMatch() ) {
            if ( matchType != HighlighterMatchType::LineMatch ) {
                matchType = HighlighterMatchType::WordMatch;
            }
            matches.addMatches( thisMatches );
        }
        else {
            matchType = HighlighterMatchType::LineMatch;
            matches.clear();
            const auto lineLengthU16 = static_cast<int64_t>(
                simdutf::utf16_length_from_utf8( line.data(), line.size() ) );
            matches.addMatch(
                { 0_lcol, LineLength{ lineLengthU16 }, hl.foreColor(), hl.backColor() } );
        }
    }

    return matchType;
}

klogg::vector<QuickHighlighter> defaultQuickHighlighters()
{
    klogg::vector<QuickHighlighter> labels;
    labels.push_back( { "Color label 1",
                        { klogg::Rgba{ 0x00, 0x1e, 0x80 }, klogg::Rgba{ 0xa1, 0xb7, 0xff } },
                        true } );
    labels.push_back( { "Color label 2",
                        { klogg::Rgba{ 0x80, 0x00, 0x5D }, klogg::Rgba{ 0xff, 0xa1, 0xc6 } },
                        true } );
    labels.push_back( { "Color label 3",
                        { klogg::Rgba{ 0x0f, 0x80, 0x00 }, klogg::Rgba{ 0xac, 0xff, 0xa1 } },
                        true } );
    labels.push_back( { "Color label 4",
                        { klogg::Rgba{ 0x80, 0x60, 0x00 }, klogg::Rgba{ 0xff, 0xe8, 0xa1 } },
                        true } );
    labels.push_back( { "Color label 5",
                        { klogg::Rgba{ 0x42, 0x00, 0x80 }, klogg::Rgba{ 0xd2, 0xa1, 0xff } },
                        true } );
    labels.push_back( { "Color label 6",
                        { klogg::Rgba{ 0x00, 0x7f, 0x80 }, klogg::Rgba{ 0xa1, 0xfe, 0xff } },
                        true } );
    labels.push_back( { "Color label 7",
                        { klogg::Rgba{ 0x00, 0x4e, 0x80 }, klogg::Rgba{ 0xa1, 0xdb, 0xff } },
                        true } );
    labels.push_back( { "Color label 8",
                        { klogg::Rgba{ 0x12, 0x00, 0x80 }, klogg::Rgba{ 0xa2, 0x9c, 0xcf } },
                        true } );
    labels.push_back( { "Color label 9",
                        { klogg::Rgba{ 0xb1, 0x4b, 0x28 }, klogg::Rgba{ 0xff, 0xd6, 0xb0 } },
                        true } );
    labels.push_back( { "Color label 10",
                        { klogg::Rgba{ 0xc2, 0x1f, 0x30 }, klogg::Rgba{ 0xff, 0xbe, 0xba } },
                        true } );
    return labels;
}
