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

#include "apisession.h"

#include <cstdlib>

#include "klogg/api.h"
#include "log.h"
#include "log_qt.h"

namespace klogg::ui {
namespace {

klogg::api::Session* registeredSession_ = nullptr;

} // namespace

klogg::api::Session& apiSession()
{
    if ( !registeredSession_ ) {
        auto session = klogg::api::Session::create();
        if ( !session ) {
            LOG_ERROR << "Failed to create API session: " << session.error().message;
            abort();
        }

        // Leaked on purpose: see the header comment
        registeredSession_ = session->release();
    }

    return *registeredSession_;
}

void setApiSession( klogg::api::Session* session )
{
    registeredSession_ = session;
}

} // namespace klogg::ui
