#include "compression.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>

namespace klogg::compression {

namespace {

constexpr std::size_t kHeadSize = 512;

std::size_t readHead( const std::filesystem::path& path, std::array<unsigned char, kHeadSize>& head )
{
    std::ifstream input( path, std::ios::binary );
    if ( !input ) {
        return 0;
    }
    input.read( reinterpret_cast<char*>( head.data() ),
                static_cast<std::streamsize>( head.size() ) );
    return static_cast<std::size_t>( input.gcount() );
}

bool matches( const std::array<unsigned char, kHeadSize>& head, std::size_t size,
              std::initializer_list<unsigned char> bytes )
{
    if ( size < bytes.size() ) {
        return false;
    }
    std::size_t index = 0;
    for ( const auto value : bytes ) {
        if ( head[ index++ ] != value ) {
            return false;
        }
    }
    return true;
}

std::string lowerCase( std::string value )
{
    std::transform( value.begin(), value.end(), value.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return value;
}

bool endsWith( const std::string& value, const std::string& suffix )
{
    return value.size() >= suffix.size()
           && value.compare( value.size() - suffix.size(), suffix.size(), suffix ) == 0;
}

ArchiveFormat formatFromExtension( const std::string& fileName )
{
    const auto lower = lowerCase( fileName );

    // Compound suffixes first: .tar.gz is a gzip stream around a tar archive.
    if ( endsWith( lower, ".tar.gz" ) ) {
        return ArchiveFormat::Gzip;
    }
    if ( endsWith( lower, ".tar.bz2" ) ) {
        return ArchiveFormat::Bzip2;
    }
    if ( endsWith( lower, ".tar.xz" ) ) {
        return ArchiveFormat::Xz;
    }
    if ( endsWith( lower, ".tgz" ) ) {
        return ArchiveFormat::Gzip;
    }
    if ( endsWith( lower, ".tbz2" ) || endsWith( lower, ".tbz" ) ) {
        return ArchiveFormat::Bzip2;
    }
    if ( endsWith( lower, ".txz" ) ) {
        return ArchiveFormat::Xz;
    }
    if ( endsWith( lower, ".gz" ) ) {
        return ArchiveFormat::Gzip;
    }
    if ( endsWith( lower, ".bz2" ) ) {
        return ArchiveFormat::Bzip2;
    }
    if ( endsWith( lower, ".xz" ) ) {
        return ArchiveFormat::Xz;
    }
    if ( endsWith( lower, ".zip" ) ) {
        return ArchiveFormat::Zip;
    }
    if ( endsWith( lower, ".7z" ) ) {
        return ArchiveFormat::SevenZip;
    }
    if ( endsWith( lower, ".tar" ) ) {
        return ArchiveFormat::Tar;
    }
    return ArchiveFormat::None;
}

} // namespace

ArchiveFormat detectArchiveFormat( const std::filesystem::path& path )
{
    std::array<unsigned char, kHeadSize> head{};
    const auto size = readHead( path, head );

    // Content wins over the name: servers often deliver compressed logs under
    // names without any suffix.
    if ( matches( head, size, { 0x1f, 0x8b } ) ) {
        return ArchiveFormat::Gzip;
    }
    if ( matches( head, size, { 'B', 'Z', 'h' } ) ) {
        return ArchiveFormat::Bzip2;
    }
    if ( matches( head, size, { 0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00 } ) ) {
        return ArchiveFormat::Xz;
    }
    if ( matches( head, size, { 'P', 'K', 0x03, 0x04 } )
         || matches( head, size, { 'P', 'K', 0x05, 0x06 } )
         || matches( head, size, { 'P', 'K', 0x07, 0x08 } ) ) {
        return ArchiveFormat::Zip;
    }
    if ( matches( head, size, { 0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c } ) ) {
        return ArchiveFormat::SevenZip;
    }
    if ( size >= 262 && std::memcmp( head.data() + 257, "ustar", 5 ) == 0 ) {
        return ArchiveFormat::Tar;
    }

    const auto name = path.filename().u8string();
    return formatFromExtension(
        std::string( reinterpret_cast<const char*>( name.data() ), name.size() ) );
}

bool isStreamCompressed( ArchiveFormat format )
{
    return format == ArchiveFormat::Gzip || format == ArchiveFormat::Bzip2
           || format == ArchiveFormat::Xz;
}

bool isMultiFileArchive( ArchiveFormat format )
{
    return format == ArchiveFormat::Zip || format == ArchiveFormat::SevenZip
           || format == ArchiveFormat::Tar;
}

const char* archiveFormatName( ArchiveFormat format )
{
    switch ( format ) {
    case ArchiveFormat::Gzip:
        return "gzip";
    case ArchiveFormat::Bzip2:
        return "bzip2";
    case ArchiveFormat::Xz:
        return "xz";
    case ArchiveFormat::Zip:
        return "zip";
    case ArchiveFormat::SevenZip:
        return "7z";
    case ArchiveFormat::Tar:
        return "tar";
    case ArchiveFormat::None:
        break;
    }
    return "none";
}

std::string decompressedFileName( const std::string& fileName )
{
    const auto lower = lowerCase( fileName );

    auto replaceSuffix = [ & ]( const std::string& suffix, const std::string& replacement ) {
        if ( endsWith( lower, suffix ) ) {
            return fileName.substr( 0, fileName.size() - suffix.size() ) + replacement;
        }
        return std::string{};
    };

    for ( const auto& pair : std::vector<std::pair<std::string, std::string>>{
              { ".tar.gz", ".tar" },
              { ".tar.bz2", ".tar" },
              { ".tar.xz", ".tar" },
              { ".tgz", ".tar" },
              { ".tbz2", ".tar" },
              { ".tbz", ".tar" },
              { ".txz", ".tar" },
              { ".gz", "" },
              { ".bz2", "" },
              { ".xz", "" } } ) {
        const auto stripped = replaceSuffix( pair.first, pair.second );
        if ( !stripped.empty() ) {
            return stripped;
        }
    }

    return fileName + ".decompressed";
}

} // namespace klogg::compression
