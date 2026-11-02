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

#ifndef KLOGG_TEST_FAKELINEDATASOURCE_H
#define KLOGG_TEST_FAKELINEDATASOURCE_H

#include <algorithm>
#include <utility>
#include <vector>

#include <QString>

#include "linedatasource.h"

// Minimal in-memory LineDataSource for driving search logic in tests without
// touching the filesystem or the indexing pipeline. Fetches are counted so
// tests can assert the batched (block) access pattern of the scanning code.
class FakeLineDataSource : public LineDataSource {
public:
    explicit FakeLineDataSource( std::vector<QString> lines )
        : lines_( std::move( lines ) )
    {
    }

    std::string getLineString( LineNumber line ) const override
    {
        ++singleLineCallCount_;
        return lines_.at( line.get() ).toStdString();
    }
    std::string getExpandedLineString( LineNumber line ) const override
    {
        ++singleLineCallCount_;
        return lines_.at( line.get() ).toStdString();
    }
    klogg::vector<std::string> getLines( LineNumber first, LinesCount number ) const override
    {
        ++getLinesCallCount_;
        lastRequestSize_ = number.get();
        klogg::vector<std::string> result;
        for ( auto i = 0u; i < number.get(); ++i ) {
            result.push_back( lines_.at( first.get() + i ).toStdString() );
        }
        return result;
    }
    LineNumber getLineNumber( LineNumber index ) const override
    {
        return index;
    }
    LinesCount getNbLine() const override
    {
        return LinesCount( static_cast<LinesCount::UnderlyingType>( lines_.size() ) );
    }
    LineLength getMaxLength() const override
    {
        LineLength::UnderlyingType max = 0;
        for ( const auto& l : lines_ ) {
            max = std::max( max, static_cast<LineLength::UnderlyingType>( l.size() ) );
        }
        return LineLength{ max };
    }
    LineLength getLineLength( LineNumber line ) const override
    {
        return LineLength{ lines_.at( line.get() ).size() };
    }

    size_t getLinesCallCount() const
    {
        return getLinesCallCount_;
    }
    size_t singleLineCallCount() const
    {
        return singleLineCallCount_;
    }
    LinesCount::UnderlyingType lastRequestSize() const
    {
        return lastRequestSize_;
    }

private:
    std::vector<QString> lines_;

    mutable size_t getLinesCallCount_ = 0;
    mutable size_t singleLineCallCount_ = 0;
    mutable LinesCount::UnderlyingType lastRequestSize_ = 0;
};

#endif // KLOGG_TEST_FAKELINEDATASOURCE_H
