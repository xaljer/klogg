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

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

namespace klogg {

struct FileHandleImpl {
    int fd = -1;
};

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

    const std::string nativePath( path );
    int fd = ::open( nativePath.c_str(), O_RDONLY );
    if ( fd < 0 ) {
        return false;
    }

    impl_->fd = fd;
    return true;
}

void FileHandle::close()
{
    if ( impl_->fd >= 0 ) {
        ::close( impl_->fd );
        impl_->fd = -1;
    }
}

bool FileHandle::isOpen() const
{
    return impl_->fd >= 0;
}

std::optional<uint64_t> FileHandle::size() const
{
    if ( !isOpen() ) {
        return std::nullopt;
    }

    struct stat fileInfo{};
    if ( ::fstat( impl_->fd, &fileInfo ) != 0 ) {
        return std::nullopt;
    }
    return static_cast<uint64_t>( fileInfo.st_size );
}

size_t FileHandle::readAt( uint64_t offset, char* buffer, size_t count )
{
    if ( !isOpen() ) {
        return 0;
    }

    const ssize_t bytesRead = ::pread( impl_->fd, buffer, count, static_cast<off_t>( offset ) );
    if ( bytesRead <= 0 ) {
        return 0;
    }
    return static_cast<size_t>( bytesRead );
}

} // namespace klogg
