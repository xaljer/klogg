#include "compression.h"

#include <archive.h>
#include <archive_entry.h>

#include "archive_open.h"
#include "paths.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>

namespace klogg::compression {

namespace {

constexpr std::size_t kBlockSize = 64 * 1024;

std::string toUtf8( const std::filesystem::path& path )
{
    const auto text = path.u8string();
    return std::string( reinterpret_cast<const char*>( text.data() ), text.size() );
}

std::string readerError( archive* reader, const char* fallback )
{
    const auto* detail = archive_error_string( reader );
    return detail != nullptr && detail[ 0 ] != '\0' ? std::string( detail ) : std::string( fallback );
}

ExtractResult failure( std::string message )
{
    ExtractResult result;
    result.ok = false;
    result.message = std::move( message );
    return result;
}

bool safeJoin( const std::filesystem::path& root, const std::string& rawName,
               std::filesystem::path& target )
{
    std::string name = rawName;
    std::replace( name.begin(), name.end(), '\\', '/' );
    while ( name.rfind( "./", 0 ) == 0 ) {
        name.erase( 0, 2 );
    }
    if ( name.empty() ) {
        return false;
    }

    const auto relative = platform::paths::pathFromUtf8( name );
    if ( relative.is_absolute() || relative.has_root_name() ) {
        return false;
    }

    const auto normalized = relative.lexically_normal();
    if ( normalized.empty() ) {
        return false;
    }
    for ( const auto& part : normalized ) {
        if ( part == ".." ) {
            return false;
        }
    }

    target = root / normalized;
    return true;
}

} // namespace

ExtractResult extractArchive( ArchiveFormat format, const std::filesystem::path& archivePath,
                              const std::filesystem::path& destination )
{
    std::error_code error;
    std::filesystem::create_directories( destination, error );
    if ( error ) {
        return failure( "cannot create extraction directory: " + error.message() );
    }

    archive* reader = archive_read_new();
    if ( reader == nullptr ) {
        return failure( "cannot create archive reader" );
    }

    archive_read_support_filter_all( reader );
    archive_read_support_format_all( reader );

    if ( detail::openArchiveReader( reader, archivePath, kBlockSize ) != ARCHIVE_OK ) {
        auto message = std::string( "cannot open archive: " ) + std::string( archiveFormatName( format ) )
                       + ": " + readerError( reader, "unknown error" );
        archive_read_free( reader );
        return failure( message );
    }

    ExtractResult result;
    archive_entry* entry = nullptr;
    bool failed = false;
    std::string failureMessage;

    while ( true ) {
        const auto status = archive_read_next_header( reader, &entry );
        if ( status == ARCHIVE_EOF ) {
            break;
        }
        if ( status != ARCHIVE_OK ) {
            failed = true;
            failureMessage = readerError( reader, "cannot read archive entry" );
            break;
        }

        const char* rawName = archive_entry_pathname_utf8( entry );
        if ( rawName == nullptr ) {
            rawName = archive_entry_pathname( entry );
        }
        if ( rawName == nullptr || archive_entry_filetype( entry ) != AE_IFREG ) {
            archive_read_data_skip( reader );
            continue;
        }

        std::filesystem::path target;
        if ( !safeJoin( destination, rawName, target ) ) {
            archive_read_data_skip( reader );
            continue;
        }

        std::filesystem::create_directories( target.parent_path(), error );
        std::ofstream output( target, std::ios::binary | std::ios::trunc );
        if ( !output ) {
            failed = true;
            failureMessage = "cannot create extracted file: " + toUtf8( target );
            break;
        }

        std::array<char, kBlockSize> buffer{};
        std::uintmax_t written = 0;
        bool entryOk = true;
        for ( ;; ) {
            const auto read = archive_read_data( reader, buffer.data(), buffer.size() );
            if ( read > 0 ) {
                output.write( buffer.data(), static_cast<std::streamsize>( read ) );
                written += static_cast<std::uintmax_t>( read );
                if ( !output ) {
                    entryOk = false;
                    break;
                }
            }
            else if ( read == 0 ) {
                break;
            }
            else {
                entryOk = false;
                break;
            }
        }
        output.close();

        if ( !entryOk ) {
            failed = true;
            failureMessage = "failed to extract " + toUtf8( target );
            break;
        }

        result.files.push_back( ExtractedFile{ target, written } );
    }

    archive_read_free( reader );

    if ( failed ) {
        return failure( failureMessage );
    }
    if ( result.files.empty() ) {
        return failure( "archive contains no regular files" );
    }

    result.ok = true;
    return result;
}

} // namespace klogg::compression
