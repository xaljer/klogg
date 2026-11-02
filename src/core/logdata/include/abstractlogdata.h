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
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#ifndef ABSTRACTLOGDATA_H
#define ABSTRACTLOGDATA_H

#include <string>

#include "encoding.h"
#include "linetypes.h"

// Base class representing a set of data.
// It can be either a full set or a filtered set.
class AbstractLogData {
public:
    virtual ~AbstractLogData() = default;

    // Returns the line passed as a UTF-8 std::string
    std::string getLineString( LineNumber line ) const;
    // Returns the line passed as a UTF-8 std::string, with tabs expanded
    std::string getExpandedLineString( LineNumber line ) const;
    // Returns a set of lines as UTF-8 std::strings
    klogg::vector<std::string> getLines( LineNumber first_line, LinesCount number ) const;
    // Returns a set of lines with tabs expanded
    klogg::vector<std::string> getExpandedLines( LineNumber first_line,
                                                 LinesCount number ) const;
    // Returns the line numer
    LineNumber getLineNumber( LineNumber index ) const;
    // Returns the total number of lines
    LinesCount getNbLine() const;
    // Returns the visible length of the longest line
    // Tabs are expanded
    LineLength getMaxLength() const;
    // Returns the visible length of the passed line
    // Tabs are expanded
    LineLength getLineLength( LineNumber line ) const;

    // Set the view to use the passed encoding for display. Returns true when
    // the change also reindexes the data, whose completion is then reported
    // asynchronously through the loading finished signal: a caller that may
    // only report the change once it took effect has to wait for it.
    bool setDisplayEncoding( const char* encoding_name );
    // Configure how the view shall interpret newline characters
    // this should be non zero for encodings where \n is encoded
    // in multiple bytes (e.g. UTF-16)
    void setMultibyteEncodingOffsets( int before_cr, int after_cr );

    std::optional<klogg::Encoding> getDisplayEncoding() const;

    void attachReader() const;
    void detachReader() const;

    // The "type" of a line, which will appear in the FilteredView
    enum class LineTypeFlags {
        Plain = 0,
        Match = 1 << 0,
        Mark = 1 << 1,
        Event = 1 << 2,
    };
    class LineType {
    public:
        constexpr LineType() = default;
        constexpr LineType( LineTypeFlags flags )
            : flags_( flags )
        {
        }

        constexpr bool testFlag( LineTypeFlags flag ) const
        {
            return ( static_cast<unsigned int>( flags_ ) & static_cast<unsigned int>( flag ) ) != 0;
        }

        constexpr LineType operator|( LineTypeFlags flags ) const
        {
            return LineType( static_cast<LineTypeFlags>(
                static_cast<unsigned int>( flags_ ) | static_cast<unsigned int>( flags ) ) );
        }

        constexpr LineType& operator|=( LineTypeFlags flags )
        {
            flags_ = static_cast<LineTypeFlags>( static_cast<unsigned int>( flags_ )
                                                  | static_cast<unsigned int>( flags ) );
            return *this;
        }

        constexpr bool operator==( const LineType& other ) const = default;

        constexpr LineTypeFlags flags() const
        {
            return flags_;
        }

    private:
        LineTypeFlags flags_ = LineTypeFlags::Plain;
    };

protected:
    // Internal function called to get a given line
    virtual std::string doGetLineString( LineNumber line ) const = 0;
    // Internal function called to get a given line
    virtual std::string doGetExpandedLineString( LineNumber line ) const = 0;
    // Internal function called to get a set of lines
    virtual klogg::vector<std::string>
    doGetLines( LineNumber first_line, LinesCount number ) const = 0;
    // Internal function called to get a set of expanded lines
    virtual klogg::vector<std::string> doGetExpandedLines( LineNumber first_line,
                                                           LinesCount number ) const
        = 0;

    // Internal function called to get the index of given line
    virtual LineNumber doGetLineNumber( LineNumber index ) const = 0;
    // Internal function called to get the number of lines
    virtual LinesCount doGetNbLine() const = 0;
    // Internal function called to get the maximum length
    virtual LineLength doGetMaxLength() const = 0;
    // Internal function called to get the line length
    virtual LineLength doGetLineLength( LineNumber line ) const = 0;
    // Internal function called to set the encoding
    virtual bool doSetDisplayEncoding( const char* encoding ) = 0;
    virtual std::optional<klogg::Encoding> doGetDisplayEncoding() const = 0;

    virtual void doAttachReader() const = 0;
    virtual void doDetachReader() const = 0;
};

constexpr AbstractLogData::LineType operator|( AbstractLogData::LineTypeFlags a,
                                               AbstractLogData::LineTypeFlags b )
{
    return AbstractLogData::LineType( a ) | b;
}

#endif
