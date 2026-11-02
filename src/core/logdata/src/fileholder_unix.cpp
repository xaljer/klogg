#include "fileholder.h"

#include <sys/stat.h>

#include "log.h"

FileId FileId::getFileId( const std::string& filename )
{
    struct stat info;
    if ( lstat( filename.c_str(), &info ) != 0 ) {
        LOG_DEBUG << "Failed to get file info for " << filename;
        return FileId{};
    }

    return FileId{ info.st_ino, static_cast<uint64_t>( info.st_dev ) };
}
