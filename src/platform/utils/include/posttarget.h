/*
 * Copyright (C) 2009 -- 2026 Klogg contributors
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

#ifndef POSTTARGET_H
#define POSTTARGET_H

#include <functional>

// Injects a delayed call into the thread owning the target object (LogData,
// FileWatcher observer, ...). The target must never run the task inline:
// completion callbacks are held on behalf of the caller, and an inline
// execution would run them on the worker thread where a nested wait would
// deadlock the operation queue. Single definition shared by the core modules.
using PostTarget = std::function<void( std::function<void()> )>;

#endif
