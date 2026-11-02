/*
 * Copyright (C) 2016 -- 2026 Anton Filimonov and other contributors
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

// Qt coupling for the line types in linetypes.h. linetypes.h itself is Qt-free;
// the pieces below (metatype registration for queued signals, QDebug streaming,
// and the QString-based tab expansion) belong on the Qt consumer side until the
// display uses the Qt-free core only.
#pragma once
#include <QDebug>
#include <QMetaType>
#include <QString>

#include <cstdint>

#include "linetypes.h"

Q_DECLARE_METATYPE( LinesCount )
Q_DECLARE_METATYPE( LineNumber )
Q_DECLARE_METATYPE( LineLength )
Q_DECLARE_METATYPE( LineColumn )

template <typename Tag, typename T>
QDebug operator<<( QDebug dbg, type_safe::strong_typedef<Tag, T> const& object )
{
    QDebugStateSaver saver( dbg );
    dbg << type_safe::get( object );

    return dbg;
}

// Tab expansion of the display text. Position arithmetic stays in the string
// index type (int on Qt5, qsizetype on Qt6) so no narrowing is introduced now
// that LineLength/LineColumn use int64_t.
inline QString untabify( QString&& line, LineColumn initialPosition = 0_lcol )
{
    line.replace( QChar::Null, QChar::Space );

    auto position = line.indexOf( QChar::Tabulation );
    const auto initialColumn = static_cast<decltype( position )>( initialPosition.get() );
    while ( position >= 0 ) {
        const auto spaces = TabStop - ( ( initialColumn + position ) % TabStop );
        line.replace( position, 1, QString( spaces, QChar::Space ) );
        position = line.indexOf( QChar::Tabulation, position );
    }

    return std::move( line );
}
