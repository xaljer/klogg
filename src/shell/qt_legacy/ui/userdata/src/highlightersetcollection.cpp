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

#include "highlightersetcollection.h"

#include <algorithm>

#include "containers.h"
#include "log.h"

klogg::vector<HighlighterSet> HighlighterSetCollection::highlighterSets() const
{
    return highlighters_;
}

void HighlighterSetCollection::setHighlighterSets(
    const klogg::vector<HighlighterSet>& highlighters )
{
    highlighters_ = highlighters;

    activeSets_.erase( std::remove_if( activeSets_.begin(), activeSets_.end(),
                                       [ this ]( const auto& setId ) { return !hasSet( setId ); } ),
                       activeSets_.end() );
    updateCombinedSet();
}

const HighlighterSet& HighlighterSetCollection::currentActiveSet() const
{
    return combinedActiveSet_;
}

void HighlighterSetCollection::rebuildCombinedSet()
{
    updateCombinedSet();
}

void HighlighterSetCollection::updateCombinedSet()
{
    klogg::vector<Highlighter> combined;

    for ( const HighlighterSet& set : klogg::as_const( highlighters_ ) ) {
        if ( std::find( activeSets_.begin(), activeSets_.end(), set.id() ) == activeSets_.end() ) {
            continue;
        }
        const auto& rules = set.highlighters();
        combined.insert( combined.end(), rules.begin(), rules.end() );
    }

    combinedActiveSet_.setHighlighters( combined );
    combinedActiveSet_.compile();
}

std::vector<std::string> HighlighterSetCollection::activeSetIds() const
{
    return activeSets_;
}

void HighlighterSetCollection::activateSet( const std::string& setId )
{
    LOG_INFO << "activating set " << setId;
    if ( !hasSet( setId )
         || std::find( activeSets_.begin(), activeSets_.end(), setId ) != activeSets_.end() ) {
        LOG_WARNING << "Set not found or already active";
        return;
    }

    activeSets_.push_back( setId );
    updateCombinedSet();
}

void HighlighterSetCollection::deactivateSet( const std::string& setId )
{
    LOG_INFO << "deactivating set " << setId;
    activeSets_.erase( std::remove( activeSets_.begin(), activeSets_.end(), setId ),
                       activeSets_.end() );
    updateCombinedSet();
}

void HighlighterSetCollection::deactivateAll()
{
    LOG_INFO << "deactivating all sets";
    activeSets_.clear();
    updateCombinedSet();
}

bool HighlighterSetCollection::hasSet( const std::string& setId ) const
{
    return std::any_of( highlighters_.begin(), highlighters_.end(),
                        [ &setId ]( const auto& s ) { return s.id() == setId; } );
}

bool HighlighterSetCollection::hasSetByName( const std::string& setName ) const
{
    return std::any_of( highlighters_.begin(), highlighters_.end(),
                        [ &setName ]( const auto& s ) { return s.name() == setName; } );
}

const klogg::vector<QuickHighlighter>& HighlighterSetCollection::quickHighlighters() const
{
    return quickHighlighters_;
}

void HighlighterSetCollection::setQuickHighlighters(
    const klogg::vector<QuickHighlighter>& quickHighlighters )
{
    quickHighlighters_ = quickHighlighters;
}
