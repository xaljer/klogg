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

#ifndef KLOGG_TEST_FAKELOGDATA_H
#define KLOGG_TEST_FAKELOGDATA_H

#include <algorithm>
#include <utility>
#include <vector>

#include <QString>

#include "abstractlogdata.h"

// Minimal in-memory AbstractLogData for driving search logic in tests without
// touching the filesystem or the indexing pipeline.
class FakeLogData : public AbstractLogData {
  public:
    explicit FakeLogData( std::vector<QString> lines )
        : lines_( std::move( lines ) )
    {
    }

  protected:
    QString doGetLineString( LineNumber line ) const override
    {
        return lines_.at( line.get() );
    }
    QString doGetExpandedLineString( LineNumber line ) const override
    {
        return lines_.at( line.get() );
    }
    klogg::vector<QString> doGetLines( LineNumber first, LinesCount number ) const override
    {
        klogg::vector<QString> result;
        for ( auto i = 0u; i < number.get(); ++i ) {
            result.push_back( lines_.at( first.get() + i ) );
        }
        return result;
    }
    klogg::vector<QString> doGetExpandedLines( LineNumber first, LinesCount number ) const override
    {
        return doGetLines( first, number );
    }
    LineNumber doGetLineNumber( LineNumber index ) const override
    {
        return index;
    }
    LinesCount doGetNbLine() const override
    {
        return LinesCount( static_cast<LinesCount::UnderlyingType>( lines_.size() ) );
    }
    LineLength doGetMaxLength() const override
    {
        int max = 0;
        for ( const auto& l : lines_ ) {
            max = std::max( max, static_cast<int>( l.size() ) );
        }
        return LineLength{ max };
    }
    LineLength doGetLineLength( LineNumber line ) const override
    {
        return LineLength{ static_cast<int>( lines_.at( line.get() ).size() ) };
    }
    void doSetDisplayEncoding( const char* ) override
    {
    }
    QTextCodec* doGetDisplayEncoding() const override
    {
        return nullptr;
    }
    void doAttachReader() const override
    {
    }
    void doDetachReader() const override
    {
    }

  private:
    std::vector<QString> lines_;
};

#endif // KLOGG_TEST_FAKELOGDATA_H
