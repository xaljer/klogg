#include "fileholder.h"

#include <filewatcher.h>

#include "log.h"

FileHolder::FileHolder( bool keepClosed )
    : keep_closed_{ keepClosed }
{
    LOG_INFO << "created file holder " << reinterpret_cast<void*>(this);
}

FileHolder::~FileHolder()
{
    LOG_INFO << "destroy file holder " << reinterpret_cast<void*>(this) << " for " << file_name_;

    try {
        // Remove the current file from the watch list
        if ( attached_file_ ) {
            FileWatcher::getFileWatcher().removeFile( file_name_ );
        }
    } catch ( const std::exception& e ) {
        LOG_ERROR << "Failed to destroy FileHolder: " << e.what();
    }
}

FileId FileHolder::getFileId()
{
    ScopedRecursiveLock locker( file_mutex_ );
    return attached_file_id_;
}

int64_t FileHolder::size()
{
    ScopedRecursiveLock locker( file_mutex_ );
    if ( !attached_file_ ) {
        return 0;
    }

    const auto fileSize = attached_file_->size();
    return fileSize ? static_cast<int64_t>( *fileSize ) : 0;
}

bool FileHolder::isOpen()
{
    ScopedRecursiveLock locker( file_mutex_ );
    return attached_file_ ? attached_file_->isOpen() : false;
}

void FileHolder::open( const std::string& fileName )
{
    ScopedRecursiveLock locker( file_mutex_ );
    file_name_ = fileName;

    LOG_INFO << "open file " << file_name_ << " keep closed " << keep_closed_;

    if ( !keep_closed_ ) {
        counter_ = 1;
        reOpenFile();
    }
}

void FileHolder::lock()
{
    file_mutex_.lock();
}

void FileHolder::unlock()
{
    file_mutex_.unlock();
}

void FileHolder::attachReader()
{
    ScopedRecursiveLock locker( file_mutex_ );

    if ( keep_closed_ && counter_ == 0 ) {
        LOG_INFO << "fist reader opened for " << file_name_;
        reOpenFile();
    }

    counter_++;

    LOG_DEBUG << "has " << counter_ << " readers for " << file_name_;
}

void FileHolder::detachReader()
{
    ScopedRecursiveLock locker( file_mutex_ );
    if ( counter_ > 0 ) {
        counter_--;
    }

    if ( keep_closed_ && counter_ == 0 ) {
        attached_file_->close();
        LOG_INFO << "last reader closed for " << file_name_;
    }
}

void FileHolder::reOpenFile()
{
    LOG_DEBUG << "reopen " << file_name_;

    auto reopened = std::make_unique<klogg::FileHandle>();
    reopened->open( file_name_ );

    ScopedRecursiveLock locker( file_mutex_ );
    attached_file_ = std::move( reopened );
    attached_file_id_ = FileId::getFileId( file_name_ );
}

klogg::FileHandle* FileHolder::getFile()
{
    return attached_file_.get();
}
