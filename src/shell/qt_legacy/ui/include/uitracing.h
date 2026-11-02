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

#pragma once

// GUI-thread tracing for the ui module, sibling of the api module's
// src/api/src/tracing.h: it shares the klogg_tracing category set (the "ui"
// category is defined there) and the same KLOGG_PERFETTO_TRACE session.
// Built only with -DKLOGG_USE_PERFETTO=ON; when off, KLOGG_UI_TRACE_* macros
// expand to nothing and not even the name strings reach the binary.

#ifdef KLOGG_USE_PERFETTO

#include "tracing.h"

#define KLOGG_UI_TRACE_SCOPE( name ) KLOGG_TRACE_SCOPE( "ui", name )
// One-shot marker for user actions (splitter drag step, tab switch, search
// click) so wall-clock windows are easy to slice in the trace
#define KLOGG_UI_TRACE_INSTANT( name ) TRACE_EVENT_INSTANT( "ui", name )

#else // KLOGG_USE_PERFETTO

#define KLOGG_UI_TRACE_SCOPE( name ) ( (void)0 )
#define KLOGG_UI_TRACE_INSTANT( name ) ( (void)0 )

#endif // KLOGG_USE_PERFETTO
