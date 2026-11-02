#include "compression.h"

#include <archive.h>
#include <archive_entry.h>

#include "archive_open.h"

#include <array>
#include <fstream>
#include <string>

namespace klogg::compression {

namespace {

constexpr std::size_t kBlockSize = 64 * 1024;

std::string readerError( archive* reader, const char* fallback )
{
    const auto* detail = archive_error_string( reader );
    return detail != nullptr && detail[ 0 ] != '\0' ? std::string( detail ) : std::string( fallback );
}

OperationResult failure( std::string message )
{
    return OperationResult{ false, std::move( message ) };
}

} // namespace

OperationResult decompressStream( ArchiveFormat format, const std::filesystem::path& input,
                                  const std::filesystem::path& output )
{
    archive* reader = archive_read_new();
    if ( reader == nullptr ) {
        return failure( "cannot create decompressor" );
    }

    archive_read_support_filter_all( reader );
    archive_read_support_format_raw( reader );

    if ( detail::openArchiveReader( reader, input, kBlockSize ) != ARCHIVE_OK ) {
        auto message = std::string( "cannot open compressed file: " )
                       + std::string( archiveFormatName( format ) ) + ": "
                       + readerError( reader, "unknown error" );
        archive_read_free( reader );
        return failure( message );
    }

    archive_entry* entry = nullptr;
    if ( archive_read_next_header( reader, &entry ) != ARCHIVE_OK ) {
        auto message = std::string( "cannot read compressed data: " )
                       + readerError( reader, "unknown error" );
        archive_read_free( reader );
        return failure( message );
    }

    std::ofstream target( output, std::ios::binary | std::ios::trunc );
    if ( !target ) {
        archive_read_free( reader );
        return failure( "cannot create decompressed file" );
    }

    std::array<char, kBlockSize> buffer{};
    bool ok = true;
    for ( ;; ) {
        const auto read = archive_read_data( reader, buffer.data(), buffer.size() );
        if ( read > 0 ) {
            target.write( buffer.data(), static_cast<std::streamsize>( read ) );
            if ( !target ) {
                ok = false;
                break;
            }
        }
        else if ( read == 0 ) {
            break;
        }
        else {
            ok = false;
            break;
        }
    }

    const auto error = readerError( reader, "" );
    archive_read_free( reader );

    if ( !ok ) {
        return failure( "decompression failed: " + error );
    }
    if ( !target ) {
        return failure( "cannot write decompressed file" );
    }
    return OperationResult{ true, {} };
}

} // namespace klogg::compression
