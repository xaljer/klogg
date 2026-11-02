#ifndef KLOGG_COMPRESSION_H
#define KLOGG_COMPRESSION_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace klogg::compression {

enum class ArchiveFormat {
    None,
    Gzip,
    Bzip2,
    Xz,
    Zip,
    SevenZip,
    Tar,
};

// Detects the format from the file contents first and falls back to the name.
ArchiveFormat detectArchiveFormat( const std::filesystem::path& path );

// gz/bz2/xz hold one compressed stream.
bool isStreamCompressed( ArchiveFormat format );

// zip/7z/tar can hold several files.
bool isMultiFileArchive( ArchiveFormat format );

const char* archiveFormatName( ArchiveFormat format );

// gz/bz2/xz file name without the compression suffix, e.g. app.log.gz -> app.log.
std::string decompressedFileName( const std::string& fileName );

struct OperationResult {
    bool ok = false;
    std::string message;
};

struct ExtractedFile {
    std::filesystem::path path;
    std::uintmax_t size = 0;
};

struct ExtractResult {
    bool ok = false;
    std::string message;
    std::vector<ExtractedFile> files;
};

// gz/bz2/xz: one output file.
OperationResult decompressStream( ArchiveFormat format, const std::filesystem::path& input,
                                  const std::filesystem::path& output );

// zip/tar: extracts the regular files under destination and reports them.
ExtractResult extractArchive( ArchiveFormat format, const std::filesystem::path& archive,
                              const std::filesystem::path& destination );

} // namespace klogg::compression

#endif // KLOGG_COMPRESSION_H
