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

#include <catch2/catch.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <bzlib.h>
#include <lzma.h>
#include <zlib.h>

#include "compression.h"

namespace {

using klogg::compression::ArchiveFormat;

const std::string kLogText = "2026-11-11 03:00:00.000 INFO first line\n"
                             "2026-11-11 03:00:01.000 ERROR second line\n"
                             "2026-11-11 03:00:02.000 WARN third line\n";

std::filesystem::path makeFixtureDir()
{
    const auto dir = std::filesystem::temp_directory_path()
                     / ( "klogg_compression_test_"
                         + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count() ) );
    std::filesystem::remove_all( dir );
    std::filesystem::create_directories( dir );
    return dir;
}

void writeBytes( const std::filesystem::path& path, const std::vector<unsigned char>& bytes )
{
    std::ofstream output( path, std::ios::binary | std::ios::trunc );
    output.write( reinterpret_cast<const char*>( bytes.data() ),
                  static_cast<std::streamsize>( bytes.size() ) );
}

std::string readText( const std::filesystem::path& path )
{
    std::ifstream input( path, std::ios::binary );
    return std::string( std::istreambuf_iterator<char>( input ),
                        std::istreambuf_iterator<char>() );
}

std::vector<unsigned char> gzipCompress( const std::string& text )
{
    z_stream stream{};
    REQUIRE( deflateInit2( &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                           Z_DEFAULT_STRATEGY )
             == Z_OK );
    std::vector<unsigned char> output( compressBound( static_cast<uLong>( text.size() ) ) + 64 );
    stream.next_in = reinterpret_cast<Bytef*>( const_cast<char*>( text.data() ) );
    stream.avail_in = static_cast<uInt>( text.size() );
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>( output.size() );
    REQUIRE( deflate( &stream, Z_FINISH ) == Z_STREAM_END );
    output.resize( output.size() - stream.avail_out );
    deflateEnd( &stream );
    return output;
}

std::vector<unsigned char> bzip2Compress( const std::string& text )
{
    std::vector<unsigned char> output( text.size() + text.size() / 100 + 600 );
    unsigned int outputSize = static_cast<unsigned int>( output.size() );
    REQUIRE( BZ2_bzBuffToBuffCompress( reinterpret_cast<char*>( output.data() ), &outputSize,
                                       const_cast<char*>( text.data() ),
                                       static_cast<unsigned int>( text.size() ), 9, 0, 30 )
             == BZ_OK );
    output.resize( outputSize );
    return output;
}

std::vector<unsigned char> xzCompress( const std::string& text )
{
    std::vector<unsigned char> output( lzma_stream_buffer_bound( text.size() ) );
    std::size_t outputPosition = 0;
    REQUIRE( lzma_easy_buffer_encode( 6, LZMA_CHECK_CRC64, nullptr,
                                      reinterpret_cast<const std::uint8_t*>( text.data() ),
                                      text.size(), output.data(), &outputPosition,
                                      output.size() )
             == LZMA_OK );
    output.resize( outputPosition );
    return output;
}

void appendLe16( std::vector<unsigned char>& out, std::uint16_t value )
{
    out.push_back( static_cast<unsigned char>( value & 0xff ) );
    out.push_back( static_cast<unsigned char>( ( value >> 8 ) & 0xff ) );
}

void appendLe32( std::vector<unsigned char>& out, std::uint32_t value )
{
    for ( int index = 0; index < 4; ++index ) {
        out.push_back( static_cast<unsigned char>( ( value >> ( index * 8 ) ) & 0xff ) );
    }
}

std::vector<unsigned char> makeStoredZip( const std::string& name, const std::string& content )
{
    std::vector<unsigned char> archive;
    const auto checksum = static_cast<std::uint32_t>(
        crc32( 0, reinterpret_cast<const Bytef*>( content.data() ),
               static_cast<uInt>( content.size() ) ) );

    const auto localOffset = static_cast<std::uint32_t>( archive.size() );
    appendLe32( archive, 0x04034b50 );
    appendLe16( archive, 20 );
    appendLe16( archive, 0 );
    appendLe16( archive, 0 );
    appendLe16( archive, 0 );
    appendLe16( archive, 0 );
    appendLe32( archive, checksum );
    appendLe32( archive, static_cast<std::uint32_t>( content.size() ) );
    appendLe32( archive, static_cast<std::uint32_t>( content.size() ) );
    appendLe16( archive, static_cast<std::uint16_t>( name.size() ) );
    appendLe16( archive, 0 );
    archive.insert( archive.end(), name.begin(), name.end() );
    archive.insert( archive.end(), content.begin(), content.end() );

    const auto directoryOffset = static_cast<std::uint32_t>( archive.size() );
    appendLe32( archive, 0x02014b50 );
    appendLe16( archive, 20 );
    appendLe16( archive, 20 );
    appendLe16( archive, 0 );
    appendLe16( archive, 0 );
    appendLe16( archive, 0 );
    appendLe16( archive, 0 );
    appendLe32( archive, checksum );
    appendLe32( archive, static_cast<std::uint32_t>( content.size() ) );
    appendLe32( archive, static_cast<std::uint32_t>( content.size() ) );
    appendLe16( archive, static_cast<std::uint16_t>( name.size() ) );
    appendLe16( archive, 0 );
    appendLe16( archive, 0 );
    appendLe16( archive, 0 );
    appendLe16( archive, 0 );
    appendLe32( archive, 0 );
    appendLe32( archive, localOffset );
    archive.insert( archive.end(), name.begin(), name.end() );

    const auto directorySize = static_cast<std::uint32_t>( archive.size() ) - directoryOffset;
    appendLe32( archive, 0x06054b50 );
    appendLe16( archive, 0 );
    appendLe16( archive, 0 );
    appendLe16( archive, 1 );
    appendLe16( archive, 1 );
    appendLe32( archive, directorySize );
    appendLe32( archive, directoryOffset );
    appendLe16( archive, 0 );

    return archive;
}

std::vector<unsigned char> makeTar( const std::string& name, const std::string& content )
{
    constexpr std::size_t block = 512;
    std::vector<unsigned char> archive( block * 3, 0 );

    std::memcpy( archive.data(), name.data(), std::min( name.size(), std::size_t( 100 ) ) );
    std::snprintf( reinterpret_cast<char*>( archive.data() ) + 100, 8, "%07o", 0644 );
    std::snprintf( reinterpret_cast<char*>( archive.data() ) + 108, 8, "%07o", 0 );
    std::snprintf( reinterpret_cast<char*>( archive.data() ) + 116, 8, "%07o", 0 );
    std::snprintf( reinterpret_cast<char*>( archive.data() ) + 124, 12, "%011llo",
                   static_cast<unsigned long long>( content.size() ) );
    std::snprintf( reinterpret_cast<char*>( archive.data() ) + 136, 12, "%011o", 0 );
    archive[ 156 ] = '0';
    std::memcpy( archive.data() + 257, "ustar", 5 );
    std::memcpy( archive.data() + 263, "00", 2 );

    std::memset( archive.data() + 148, ' ', 8 );
    unsigned int checksum = 0;
    for ( std::size_t index = 0; index < block; ++index ) {
        checksum += archive[ index ];
    }
    std::snprintf( reinterpret_cast<char*>( archive.data() ) + 148, 8, "%06o", checksum );

    std::memcpy( archive.data() + block, content.data(), content.size() );
    return archive;
}

} // namespace

TEST_CASE( "Detects archive formats from content", "[compression]" )
{
    const auto dir = makeFixtureDir();
    const auto text = dir / "app.log";
    writeBytes( text, std::vector<unsigned char>( kLogText.begin(), kLogText.end() ) );

    const auto gzipPath = dir / "app.log.gz";
    writeBytes( gzipPath, gzipCompress( kLogText ) );
    CHECK( klogg::compression::detectArchiveFormat( gzipPath ) == ArchiveFormat::Gzip );

    const auto bzipPath = dir / "app.log.bz2";
    writeBytes( bzipPath, bzip2Compress( kLogText ) );
    CHECK( klogg::compression::detectArchiveFormat( bzipPath ) == ArchiveFormat::Bzip2 );

    const auto xzPath = dir / "app.log.xz";
    writeBytes( xzPath, xzCompress( kLogText ) );
    CHECK( klogg::compression::detectArchiveFormat( xzPath ) == ArchiveFormat::Xz );

    const auto zipPath = dir / "logs.zip";
    writeBytes( zipPath, makeStoredZip( "app.log", kLogText ) );
    CHECK( klogg::compression::detectArchiveFormat( zipPath ) == ArchiveFormat::Zip );

    const auto tarPath = dir / "logs.tar";
    writeBytes( tarPath, makeTar( "app.log", kLogText ) );
    CHECK( klogg::compression::detectArchiveFormat( tarPath ) == ArchiveFormat::Tar );

    // Content wins over an unrelated name.
    const auto misnamed = dir / "download.bin";
    writeBytes( misnamed, gzipCompress( kLogText ) );
    CHECK( klogg::compression::detectArchiveFormat( misnamed ) == ArchiveFormat::Gzip );

    std::filesystem::remove_all( dir );
}

TEST_CASE( "Decompresses gzip, bzip2 and xz streams", "[compression]" )
{
    const auto dir = makeFixtureDir();

    for ( const auto& item :
          std::vector<std::pair<std::string, std::vector<unsigned char>>>{
              { "app.log.gz", gzipCompress( kLogText ) },
              { "app.log.bz2", bzip2Compress( kLogText ) },
              { "app.log.xz", xzCompress( kLogText ) } } ) {
        const auto input = dir / item.first;
        writeBytes( input, item.second );
        const auto output = dir / ( item.first + ".out" );
        const auto result = klogg::compression::decompressStream(
            klogg::compression::detectArchiveFormat( input ), input, output );
        INFO( item.first << ": " << result.message );
        CHECK( result.ok );
        CHECK( readText( output ) == kLogText );
    }

    std::filesystem::remove_all( dir );
}

TEST_CASE( "Extracts zip and tar archives", "[compression]" )
{
    const auto dir = makeFixtureDir();

    const auto zipPath = dir / "logs.zip";
    writeBytes( zipPath, makeStoredZip( "app.log", kLogText ) );
    const auto zipResult = klogg::compression::extractArchive(
        ArchiveFormat::Zip, zipPath, dir / "zip-out" );
    INFO( zipResult.message );
    REQUIRE( zipResult.ok );
    REQUIRE( zipResult.files.size() == 1 );
    CHECK( zipResult.files.front().path.filename() == "app.log" );
    CHECK( readText( zipResult.files.front().path ) == kLogText );

    const auto tarPath = dir / "logs.tar";
    writeBytes( tarPath, makeTar( "app.log", kLogText ) );
    const auto tarResult = klogg::compression::extractArchive(
        ArchiveFormat::Tar, tarPath, dir / "tar-out" );
    INFO( tarResult.message );
    REQUIRE( tarResult.ok );
    REQUIRE( tarResult.files.size() == 1 );
    CHECK( readText( tarResult.files.front().path ) == kLogText );

    std::filesystem::remove_all( dir );
}

TEST_CASE( "Extracts 7z archives and rejects path escapes", "[compression]" )
{
    const auto dir = makeFixtureDir();

    const auto copyArchive = std::filesystem::path( KLOGG_COMPRESSION_TEST_DATA ) / "sample.7z";
    const auto lzmaArchive
        = std::filesystem::path( KLOGG_COMPRESSION_TEST_DATA ) / "sample-lzma2.7z";
    REQUIRE( std::filesystem::exists( copyArchive ) );
    REQUIRE( std::filesystem::exists( lzmaArchive ) );

    CHECK( klogg::compression::detectArchiveFormat( copyArchive ) == ArchiveFormat::SevenZip );

    for ( const auto& archive : { copyArchive, lzmaArchive } ) {
        const auto result = klogg::compression::extractArchive(
            ArchiveFormat::SevenZip, archive, dir / archive.stem() );
        INFO( archive.string() << ": " << result.message );
        REQUIRE( result.ok );
        CHECK_FALSE( result.files.empty() );
        for ( const auto& file : result.files ) {
            CHECK( std::filesystem::exists( file.path ) );
        }
    }

    const auto zipPath = dir / "escape.zip";
    writeBytes( zipPath, makeStoredZip( "../escape.log", kLogText ) );
    const auto escapeResult = klogg::compression::extractArchive(
        ArchiveFormat::Zip, zipPath, dir / "zip-escape" );
    CHECK_FALSE( escapeResult.ok );
    CHECK_FALSE( std::filesystem::exists( dir / "escape.log" ) );

    std::filesystem::remove_all( dir );
}
TEST_CASE( "Derives decompressed file names", "[compression]" )
{
    CHECK( klogg::compression::decompressedFileName( "app.log.gz" ) == "app.log" );
    CHECK( klogg::compression::decompressedFileName( "app.log.bz2" ) == "app.log" );
    CHECK( klogg::compression::decompressedFileName( "app.log.xz" ) == "app.log" );
    CHECK( klogg::compression::decompressedFileName( "bundle.tar.gz" ) == "bundle.tar" );
    CHECK( klogg::compression::decompressedFileName( "bundle.tgz" ) == "bundle.tar" );
}
