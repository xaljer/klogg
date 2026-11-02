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

#ifndef KLOGG_FILEHANDLE_H
#define KLOGG_FILEHANDLE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace klogg {

struct FileHandleImpl;

// Thin, Qt-free wrapper over a platform file handle, exposing only the two read
// paths the engine needs: sequential scans (read consecutive offsets) and
// random access by byte offset (read a block at a given position). It does not
// manage any locks; the engine already serialises the reads it performs.
//
// Non-thread-safe: a single FileHandle must be used from one thread at a time.
class FileHandle {
public:
    FileHandle();
    ~FileHandle();
    FileHandle( const FileHandle& ) = delete;
    FileHandle& operator=( const FileHandle& ) = delete;
    FileHandle( FileHandle&& ) noexcept;
    FileHandle& operator=( FileHandle&& ) noexcept;

    // Opens path (UTF-8) read-only, sharing the file with readers and writers so
    // a tailing log file can be renamed or deleted while it is held open.
    bool open( std::string_view path );
    void close();
    bool isOpen() const;

    std::optional<uint64_t> size() const;

    // Attempts to read up to count bytes starting at byte offset. Returns bytes
    // actually read; 0 when offset is at/past EOF or on an I/O error (the two
    // are deliberately conflated for this thin wrapper).
    size_t readAt( uint64_t offset, char* buffer, size_t count );

private:
    std::unique_ptr<FileHandleImpl> impl_;
};

} // namespace klogg

#endif // KLOGG_FILEHANDLE_H
