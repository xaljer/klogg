#ifndef KLOGG_COMPRESSION_ARCHIVE_OPEN_H
#define KLOGG_COMPRESSION_ARCHIVE_OPEN_H

#include <archive.h>

#include <cstddef>
#include <filesystem>

namespace klogg::compression::detail {

// libarchive's narrow open uses the ANSI code page on Windows, which breaks
// non-ASCII paths; the wide variant keeps UTF-16 file names intact.
inline int openArchiveReader( archive* reader, const std::filesystem::path& path,
                              std::size_t blockSize )
{
#ifdef _WIN32
    const auto wide = path.wstring();
    return archive_read_open_filename_w( reader, wide.c_str(), blockSize );
#else
    return archive_read_open_filename( reader, path.c_str(), blockSize );
#endif
}

} // namespace klogg::compression::detail

#endif // KLOGG_COMPRESSION_ARCHIVE_OPEN_H
