#include "fileholder.h"

#include <windows.h>

#include <filesystem>

#include "log.h"
#include "paths.h"

FileId FileId::getFileId( const std::string& filename )
{
    DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

    int accessRights = GENERIC_READ;
    DWORD creationDisp = OPEN_EXISTING;

    // Create the file handle.
    SECURITY_ATTRIBUTES securityAtts = { sizeof( SECURITY_ATTRIBUTES ), NULL, FALSE };

    const auto wideFilename = platform::paths::pathFromUtf8( filename ).wstring();

    HANDLE fileHandle
        = CreateFileW( wideFilename.c_str(), accessRights, shareMode, &securityAtts, creationDisp,
                       FILE_FLAG_BACKUP_SEMANTICS, NULL );

    if ( fileHandle == INVALID_HANDLE_VALUE ) {
        LOG_DEBUG << "Failed to get file info for " << filename << ", gle " << ::GetLastError();
        return FileId{};
    }

    using FileHandleGuard = std::unique_ptr<void, decltype( &CloseHandle )>;
    auto fileHandleGuard = FileHandleGuard{ fileHandle, CloseHandle };

    BY_HANDLE_FILE_INFORMATION info;
    if ( !::GetFileInformationByHandle( fileHandle, &info ) ) {
        LOG_DEBUG << "Failed to get file info for " << filename << ", gle " << ::GetLastError();
        return FileId{};
    }

    ULARGE_INTEGER fileIndex = { info.nFileIndexLow, info.nFileIndexHigh };
    return FileId{ fileIndex.QuadPart, static_cast<uint64_t>( info.dwVolumeSerialNumber ) };
}
