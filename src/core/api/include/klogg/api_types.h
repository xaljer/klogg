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

#include <cstdint>
#include <functional>
#include <string>

#include <tl/expected.hpp>

#include "linetypes.h"

// Base types shared by the engine and shared-state API surfaces.
namespace klogg::api {

// Strong types reused from the core
using ::FilePosition;
using ::LineColumn;
using ::LineLength;
using ::LineNumber;
using ::LinesCount;
using ::OffsetInFile;

using FileId = uint64_t;
using SearchId = uint64_t;

enum class ErrorCode {
    FileNotFound,
    FileNotOpen,
    EncodingError,
    SearchCancelled,
    InvalidPattern,
    InvalidFileId,
    InvalidSearchId,
    InvalidExtractionId,
    ExtractionCancelled,
    InternalError,
};

struct Error {
    ErrorCode code;
    std::string message;
};

// tl::expected until the project moves to C++23 std::expected
template <typename T>
using Result = tl::expected<T, Error>;

using EventHandler = std::function<void( const std::string& eventType, const std::string& json )>;

} // namespace klogg::api
