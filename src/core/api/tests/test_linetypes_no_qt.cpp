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

// Linetypes no-Qt guard. Includes only linetypes.h on purpose: if that header
// ever pulls in Qt (or a Qt-transitive header), the QT_VERSION_CHECK guard below
// fails the build, and the static_asserts pin the underlying types to int64_t.
#include <linetypes.h>

#ifdef QT_VERSION_CHECK
#error "linetypes.h must stay Qt-free; a Qt header leaked in"
#endif

static_assert( std::is_same_v<LineLength::UnderlyingType, int64_t>,
               "LineLength must keep int64_t as its underlying type" );
static_assert( std::is_same_v<LineColumn::UnderlyingType, int64_t>,
               "LineColumn must keep int64_t as its underlying type" );
