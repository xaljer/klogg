/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
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

#ifndef highlighterSet_H
#define highlighterSet_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "color.h"
#include "containers.h"
#include "highlightedmatch.h"
#include "pcre2regex.h"
#include "regularexpression.h"
#include "regularexpressionpattern.h"

struct HighlightColor {
    klogg::Rgba foreColor;
    klogg::Rgba backColor;
};

// Represents a filter, i.e. a regexp and the colors matching text
// should be rendered in.
class Highlighter {
public:
    // Construct an uninitialized Highlighter (when reading from a config file)
    Highlighter() = default;
    Highlighter( const std::string& pattern, bool ignoreCase, bool onlyMatch,
                 const klogg::Rgba& foreColor, const klogg::Rgba& backColor );

    Highlighter( const Highlighter& other );
    Highlighter& operator=( const Highlighter& other );
    Highlighter( Highlighter&& ) = default;
    Highlighter& operator=( Highlighter&& ) = default;

    bool matchLine( std::string_view line, klogg::vector<HighlightedMatch>& matches ) const;

    // Accessor functions
    std::string pattern() const;
    void setPattern( const std::string& pattern );
    bool ignoreCase() const;
    void setIgnoreCase( bool ignoreCase );

    // Optional display name; empty means unnamed (callers fall back to the pattern)
    std::string name() const;
    void setName( const std::string& name );

    bool isEnabled() const;
    void setEnabled( bool enabled );

    bool highlightOnlyMatch() const;
    void setHighlightOnlyMatch( bool onlyMatch );

    bool useRegex() const;
    void setUseRegex( bool useRegex );

    bool variateColors() const;
    void setVariateColors( bool variateColors );

    int colorVariance() const;
    void setColorVariance( int colorVariance );

    klogg::Rgba foreColor() const;
    void setForeColor( const klogg::Rgba& foreColor );
    klogg::Rgba backColor() const;
    void setBackColor( const klogg::Rgba& backColor );
    bool useThemeBack() const;
    void setUseThemeBack( bool useTheme );

    RegularExpressionPattern expressionPattern() const;

    void compile() const;

private:
    std::pair<klogg::Rgba, klogg::Rgba> vairateColors( std::string_view match ) const;

private:
    klogg::Pcre2Regex regexp_;

    mutable std::optional<klogg::Pcre2Regex> optimizedRegexp_;
    mutable int captureCountCache_ = -1;

    std::string name_;
    bool enabled_ = true;
    bool useRegex_ = true;
    bool highlightOnlyMatch_ = false;
    bool variateColors_ = false;
    int colorVariance_ = 15;

    HighlightColor color_;
    bool useThemeBack_ = true;
};

enum class HighlighterMatchType { NoMatch, WordMatch, LineMatch };

// Represents an ordered set of filters to be applied to each line displayed.
class HighlighterSet {
public:
    static HighlighterSet createNewSet( const std::string& name );

    HighlighterSet() = default;

    std::string name() const;
    std::string id() const;
    // Restores the persisted identity; createNewSet generates a fresh one
    void setId( const std::string& id );

    // Returns weither the passed line match a filter of the set,
    // if so, it returns the fore/back colors the line should use.
    HighlighterMatchType matchLine( std::string_view line, HighlightedMatchRanges& matches ) const;

    bool isEmpty() const;

    const klogg::vector<Highlighter>& highlighters() const;
    // Mutable access for the (UI) set editor; keeps the class open for editing
    // without friend declarations that would name upper-layer classes here.
    klogg::vector<Highlighter>& highlighters();
    void setName( const std::string& name );
    void setHighlighters( const klogg::vector<Highlighter>& highlighters );

    void compile() const;

private:
    explicit HighlighterSet( const std::string& name );

private:
    std::string name_;
    std::string id_;
    klogg::vector<Highlighter> highlighterList_;

    mutable std::shared_ptr<MultiRegularExpression> compiledExpression_;
};

struct QuickHighlighter {
    std::string name;
    HighlightColor color;
    bool useInCycle;
};

// Default color-label palette used when nothing has been persisted yet; the
// persistence layer (UI) owns the on-disk copy and pushes it through the API.
klogg::vector<QuickHighlighter> defaultQuickHighlighters();

#endif
