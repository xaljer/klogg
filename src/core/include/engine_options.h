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

#ifndef KLOGG_ENGINE_OPTIONS_H
#define KLOGG_ENGINE_OPTIONS_H

#include <mutex>
#include <utility>

// Which regular expression engine the search pipeline uses.
enum class RegexpEngine { Hyperscan, Pcre2 };

// How ANSI escape sequences are treated when a line is displayed.
enum class AnsiProcessing {
    None = 0,
    StripOnly = 1,
    RenderColors = 2,
};

// Explicit engine behavior parameters, injected by the caller (shell/app) so
// core never reads a process-global configuration. Defaults match the
// historical configuration defaults.
struct EngineOptions {
    bool keepFileClosed = true;
    int defaultEncodingMib = -1;

    bool useCompressedIndex = true;
    bool fastModificationDetection = false;
    int indexReadBufferSizeMb = 16;

    bool useSearchResultsCache = true;
    unsigned searchResultsCacheLines = 1000000;
    bool useParallelSearch = true;
    int searchThreadPoolSize = 0;
    int searchReadBufferSizeLines = 10000;

    RegexpEngine regexpEngine = RegexpEngine::Hyperscan;
    AnsiProcessing ansiProcessing = AnsiProcessing::None;
};

// Shared, mutable session options. Engine objects hold a reference to this
// holder and read the current values at each operation, so runtime changes
// (Session::updateOptions) take effect without a process-global.
class EngineOptionsHolder {
  public:
    EngineOptions get() const
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        return options_;
    }

    void set( EngineOptions options )
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        options_ = std::move( options );
    }

  private:
    mutable std::mutex mutex_;
    EngineOptions options_;
};

#endif // KLOGG_ENGINE_OPTIONS_H
