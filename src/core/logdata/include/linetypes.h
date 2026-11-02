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

#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>

#include <type_safe/narrow_cast.hpp>
#include <type_safe/strong_typedef.hpp>

#include "containers.h"

template <typename StrongType>
constexpr StrongType maxValue()
{
    return StrongType( std::numeric_limits<typename StrongType::UnderlyingType>::max() );
}

// Qt file reading api has qint64 type offsets
struct OffsetInFile : type_safe::strong_typedef<OffsetInFile, int64_t>,
                      type_safe::strong_typedef_op::addition<OffsetInFile>,
                      type_safe::strong_typedef_op::subtraction<OffsetInFile>,
                      type_safe::strong_typedef_op::increment<OffsetInFile>,
                      type_safe::strong_typedef_op::relational_comparison<OffsetInFile>,
                      type_safe::strong_typedef_op::equality_comparison<OffsetInFile> {
    using strong_typedef::strong_typedef;

    using UnderlyingType = int64_t;

    template <typename T = UnderlyingType>
    constexpr T get() const
    {
        const auto underlyingValue = type_safe::get( *this );

        if constexpr ( std::is_same_v<T, UnderlyingType> ) {
            return underlyingValue;
        }
        else if constexpr ( std::is_unsigned_v<T> ) {
            assert( underlyingValue >= 0 );
            return type_safe::narrow_cast<T>( static_cast<uint64_t>( underlyingValue ) );
        }
        else {
            return type_safe::narrow_cast<T>( underlyingValue );
        }
    }
};

struct LinesCount : type_safe::strong_typedef<LinesCount, uint64_t>,
                    type_safe::strong_typedef_op::addition<LinesCount>,
                    type_safe::strong_typedef_op::subtraction<LinesCount>,
                    type_safe::strong_typedef_op::increment<LinesCount>,
                    type_safe::strong_typedef_op::decrement<LinesCount>,
                    type_safe::strong_typedef_op::relational_comparison<LinesCount>,
                    type_safe::strong_typedef_op::equality_comparison<LinesCount> {
    using strong_typedef::strong_typedef;

    using UnderlyingType = uint64_t;

    template <typename T = UnderlyingType>
    constexpr T get() const
    {
        static_assert( std::is_signed_v<T> == std::is_signed_v<UnderlyingType>,
                       "T should be the same sign as UnderlyingType" );

        if constexpr ( std::is_same_v<T, UnderlyingType> ) {
            return type_safe::get( *this );
        }
        else {
            return type_safe::narrow_cast<T>( type_safe::get( *this ) );
        }
    }
};

struct LineNumber : type_safe::strong_typedef<LineNumber, uint64_t>,
                    type_safe::strong_typedef_op::increment<LineNumber>,
                    type_safe::strong_typedef_op::decrement<LineNumber>,
                    type_safe::strong_typedef_op::relational_comparison<LineNumber>,
                    type_safe::strong_typedef_op::equality_comparison<LineNumber> {
    using strong_typedef::strong_typedef;

    using UnderlyingType = uint64_t;

    template <typename T = UnderlyingType>
    constexpr T get() const
    {
        if constexpr ( std::is_same_v<T, UnderlyingType> ) {
            return type_safe::get( *this );
        }
        else {
            return type_safe::narrow_cast<T>( type_safe::get( *this ) );
        }
    }
};

struct LineLength : type_safe::strong_typedef<LineLength, int64_t>,
                    type_safe::strong_typedef_op::addition<LineLength>,
                    type_safe::strong_typedef_op::subtraction<LineLength>,
                    type_safe::strong_typedef_op::relational_comparison<LineLength>,
                    type_safe::strong_typedef_op::equality_comparison<LineLength> {
    using strong_typedef::strong_typedef;

    using UnderlyingType = int64_t;

    template <typename T = UnderlyingType>
    constexpr T get() const
    {
        static_assert( std::is_signed_v<T> == std::is_signed_v<UnderlyingType>,
                       "T should be the same sign as UnderlyingType" );

        if constexpr ( std::is_same_v<T, UnderlyingType> ) {
            return type_safe::get( *this );
        }
        else {
            return type_safe::narrow_cast<T>( type_safe::get( *this ) );
        }
    }
};

struct LineColumn : type_safe::strong_typedef<LineColumn, int64_t>,
                    type_safe::strong_typedef_op::increment<LineColumn>,
                    type_safe::strong_typedef_op::relational_comparison<LineColumn>,
                    type_safe::strong_typedef_op::equality_comparison<LineColumn> {
    using strong_typedef::strong_typedef;

    using UnderlyingType = int64_t;

    template <typename T = UnderlyingType>
    constexpr T get() const
    {
        if constexpr ( std::is_same_v<T, UnderlyingType> ) {
            return type_safe::get( *this );
        }
        else if constexpr ( std::is_same_v<T, size_t> ) {
            assert( type_safe::get( *this ) >= 0 );
            return static_cast<T>( type_safe::get( *this ) );
        }
        else {
            static_assert( std::is_signed_v<T> == std::is_signed_v<UnderlyingType>,
                           "T should be the same sign as UnderlyingType" );
            return type_safe::narrow_cast<T>( type_safe::get( *this ) );
        }
    }
};

inline constexpr OffsetInFile operator"" _offset( unsigned long long int value )
{
    return OffsetInFile( static_cast<OffsetInFile::UnderlyingType>( value ) );
}
inline constexpr LineNumber operator"" _lnum( unsigned long long int value )
{
    return LineNumber( static_cast<LineNumber::UnderlyingType>( value ) );
}
inline constexpr LinesCount operator"" _lcount( unsigned long long int value )
{
    return LinesCount( static_cast<LinesCount::UnderlyingType>( value ) );
}
inline constexpr LineLength operator"" _length( unsigned long long int value )
{
    return LineLength( static_cast<LineLength::UnderlyingType>( value ) );
}
inline constexpr LineColumn operator"" _lcol( unsigned long long int value )
{
    return LineColumn( static_cast<LineColumn::UnderlyingType>( value ) );
}

template <typename T, typename U, typename R = T>
inline R boundedAdd( const T& value, const U& inc )
{
    return ( value.get() <= maxValue<R>().get() - inc.get() ) ? R( value.get() + inc.get() )
                                                              : maxValue<R>();
}

template <typename T, typename U, typename R = T>
inline R boundedSubtract( const T& value, const U& dec )
{
    return value.get() >= dec.get() ? R( value.get() - dec.get() ) : R{};
}

inline LineNumber& operator+=( LineNumber& number, const LinesCount& count )
{
    number = boundedAdd( number, count );
    return number;
}
inline LineNumber operator+( const LineNumber& number, const LinesCount& count )
{
    return boundedAdd( number, count );
}

inline LineColumn& operator+=( LineColumn& column, const LineLength& length )
{
    column = boundedAdd( column, length );
    return column;
}
inline LineColumn operator+( const LineColumn& column, const LineLength& length )
{
    return boundedAdd( column, length );
}

inline LineNumber operator-( const LineNumber& number, const LinesCount& count )
{
    return boundedSubtract( number, count );
}

inline LineColumn operator-( const LineColumn& column, const LineLength& length )
{
    return boundedSubtract( column, length );
}

inline LinesCount operator-( const LineNumber& n1, const LineNumber& n2 )
{
    return boundedSubtract<LineNumber, LineNumber, LinesCount>( n1, n2 );
}

inline LineLength operator-( const LineColumn& n1, const LineColumn& n2 )
{
    return boundedSubtract<LineColumn, LineColumn, LineLength>( n1, n2 );
}

inline bool operator<( const LineNumber& number, const LinesCount& count )
{
    return number.get() < count.get();
}
inline bool operator>=( const LineNumber& number, const LinesCount& count )
{
    return !( number < count );
}

using OptionalLineNumber = std::optional<LineNumber>;

// Represents a position in a file (line, column)
class FilePosition {
public:
    FilePosition()
        : column_{ -1 }
    {
    }
    FilePosition( LineNumber line, LineColumn column )
        : line_{ line }
        , column_{ column }
    {
    }

    LineNumber line() const
    {
        return line_;
    }
    LineColumn column() const
    {
        return column_;
    }

    bool operator==( const FilePosition& other ) const
    {
        return line_ == other.line_ && column_ == other.column_;
    }

    bool operator!=( const FilePosition& other ) const
    {
        return !this->operator==( other );
    }

private:
    LineNumber line_;
    LineColumn column_;
};

// Length of a tab stop
constexpr int TabStop = 8;

template <typename LineType>
LineLength getUntabifiedLength( const LineType& utf8Line )
{
    size_t totalSpaces = 0;

    auto tabPosition = utf8Line.find( '\t' );
    while ( tabPosition != std::string_view::npos ) {
        const auto spaces = TabStop - ( ( tabPosition + totalSpaces ) % TabStop ) - 1;
        totalSpaces += spaces;

        tabPosition = utf8Line.find( '\t', tabPosition + 1 );
    }

    return LineLength( type_safe::narrow_cast<LineLength::UnderlyingType>(
        static_cast<int64_t>( utf8Line.size() + totalSpaces ) ) );
}

// Tab expansion of byte text. Column arithmetic is in bytes, consistent with
// the byte-based display lengths used by the index; the pair with the
// QString-based overload in linetypes_qt.h (same tab-stop math) lives on the
// Qt consumer side until the display uses the Qt-free core only.
inline std::string untabify( std::string&& line, LineColumn initialPosition = 0_lcol )
{
    std::replace( line.begin(), line.end(), '\0', ' ' );

    const auto initialColumn = initialPosition.get();
    auto position = line.find( '\t' );
    while ( position != std::string::npos ) {
        const auto spaces
            = TabStop - ( ( initialColumn + static_cast<int64_t>( position ) ) % TabStop );
        line.replace( position, 1, std::string( static_cast<size_t>( spaces ), ' ' ) );
        position = line.find( '\t', position );
    }

    return std::move( line );
}

// iostream adapter for the strong typedefs (the Qt side-car has the QDebug
// equivalent); found through ADL because the tag types live in this namespace.
template <typename Tag, typename T>
std::ostream& operator<<( std::ostream& os, const type_safe::strong_typedef<Tag, T>& value )
{
    return os << type_safe::get( value );
}
