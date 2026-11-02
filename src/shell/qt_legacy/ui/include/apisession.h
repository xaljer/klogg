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

#ifndef KLOGG_APISESSION_H
#define KLOGG_APISESSION_H

namespace klogg::api {
class Session;
}

namespace klogg::ui {

// The process-wide Core API session shared by the UI. KloggApp registers the
// instance it owns at startup; when none is registered (e.g. widget tests) one
// is created lazily and intentionally leaked, as tearing the worker thread
// down after QApplication is gone is not safe.
klogg::api::Session& apiSession();

// Non-owning registration; the caller keeps the session alive
void setApiSession( klogg::api::Session* session );

} // namespace klogg::ui

#endif // KLOGG_APISESSION_H
