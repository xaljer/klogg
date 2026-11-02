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

#include "filehandle.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace klogg {

struct FileHandleImpl {
    HANDLE handle = INVALID_HANDLE_VALUE;
};

namespace {
constexpr DWORD kShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
} // namespace

FileHandle::FileHandle()
    : impl_( std::make_unique<FileHandleImpl>() )
{
}

FileHandle::~FileHandle()
{
    close();
}

FileHandle::FileHandle( FileHandle&& other ) noexcept
{
    impl_ = std::move( other.impl_ );
    other.impl_ = std::make_unique<FileHandleImpl>();
}

FileHandle& FileHandle::operator=( FileHandle&& other ) noexcept
{
    if ( this != &other ) {
        close();
        impl_ = std::move( other.impl_ );
        other.impl_ = std::make_unique<FileHandleImpl>();
    }
    return *this;
}

bool FileHandle::open( std::string_view path )
{
    close();

    // UTF-8 path -> UTF-16 for CreateFileW. A thin wrapper, so the conversion is
    // done here with the Win32 API rather than pulling in an existing helper.
    const int wideLength = MultiByteToWideChar( CP_UTF8, 0, path.data(),
                                                static_cast<int>( path.size() ), nullptr, 0 );
    if ( wideLength <= 0 ) {
        return false;
    }

    std::wstring widePath( static_cast<size_t>( wideLength ), L'\0' );
    MultiByteToWideChar( CP_UTF8, 0, path.data(), static_cast<int>( path.size() ), &widePath[ 0 ],
                         wideLength );

    // FILE_SHARE_DELETE keeps an open (tailing) log from being locked against a
    // concurrent rename or delete by the writer.
    HANDLE handle = CreateFileW( widePath.c_str(), GENERIC_READ, kShareMode, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( handle == INVALID_HANDLE_VALUE ) {
        return false;
    }

    impl_->handle = handle;
    return true;
}

void FileHandle::close()
{
    if ( impl_->handle != INVALID_HANDLE_VALUE ) {
        CloseHandle( impl_->handle );
        impl_->handle = INVALID_HANDLE_VALUE;
    }
}

bool FileHandle::isOpen() const
{
    return impl_->handle != INVALID_HANDLE_VALUE;
}

std::optional<uint64_t> FileHandle::size() const
{
    if ( !isOpen() ) {
        return std::nullopt;
    }

    LARGE_INTEGER fileSize;
    if ( !GetFileSizeEx( impl_->handle, &fileSize ) ) {
        return std::nullopt;
    }
    return static_cast<uint64_t>( fileSize.QuadPart );
}

size_t FileHandle::readAt( uint64_t offset, char* buffer, size_t count )
{
    if ( !isOpen() ) {
        return 0;
    }

    // Synchronous handle: an OVERLAPPED with an offset makes ReadFile start at a
    // position without moving the file pointer, matching pread.
    OVERLAPPED overlapped = {};
    overlapped.Offset = static_cast<DWORD>( offset & 0xFFFFffff );
    overlapped.OffsetHigh = static_cast<DWORD>( offset >> 32 );

    const DWORD toRead = static_cast<DWORD>( std::min<size_t>( count, MAXDWORD ) );
    DWORD bytesRead = 0;
    if ( !ReadFile( impl_->handle, buffer, toRead, &bytesRead, &overlapped ) ) {
        return 0;
    }
    return static_cast<size_t>( bytesRead );
}

} // namespace klogg
