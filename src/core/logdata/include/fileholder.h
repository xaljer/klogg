#ifndef FILEHOLDER_H
#define FILEHOLDER_H

#include <cstdint>
#include <memory>
#include <string>

#include "filehandle.h"
#include "synchronization.h"

struct FileId {
    uint64_t fileIndex = 0;
    uint64_t volumeIndex = 0;

    bool operator!=( const FileId& other ) const
    {
        return fileIndex != other.fileIndex || volumeIndex != other.volumeIndex;
    }

    static FileId getFileId( const std::string& filename );
};

template <typename T> class ScopedFileHolder {
  public:
    explicit ScopedFileHolder( T* file )
        : file_holder_( file )
    {
        file_holder_->lock();
        file_holder_->attachReader();
    }

    ~ScopedFileHolder()
    {
        file_holder_->unlock();
        file_holder_->detachReader();
    }

    klogg::FileHandle* getFile()
    {
        return file_holder_->getFile();
    }

  private:
    ScopedFileHolder( const ScopedFileHolder& ) = delete;
    ScopedFileHolder& operator=( const ScopedFileHolder& ) = delete;

    T* file_holder_;
};

class FileHolder {
    friend class ScopedFileHolder<FileHolder>;

  public:
    explicit FileHolder( bool keepClosed );
    ~FileHolder();
    FileId getFileId();
    int64_t size();

    bool isOpen();

    void open( const std::string& fileName );

    void lock();
    void unlock();

    void attachReader();
    void detachReader();

    void reOpenFile();

  private:
    FileHolder( const FileHolder& ) = delete;
    FileHolder& operator=( const FileHolder& ) = delete;

    klogg::FileHandle* getFile();

  private:
    RecursiveMutex file_mutex_;

    std::string file_name_;
    std::unique_ptr<klogg::FileHandle> attached_file_;
    FileId attached_file_id_;

    uint32_t counter_ = 0;
    bool keep_closed_ = false;
};

#endif // FILEHOLDER_H
