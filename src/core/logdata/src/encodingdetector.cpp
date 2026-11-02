/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#include "encodingdetector.h"

#include "encdecoders.h"

#include <simdutf.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using klogg::EncodingKind;

// Append the UTF-8 encoding of a stream of UTF-16 code units. Surrogate pairs
// are combined into a 4-byte sequence; an unpaired surrogate is rendered as
// '?' as the reference implementation did.
static std::string unitsToUtf8( const std::vector<char16_t>& units )
{
    std::string out;
    out.reserve( units.size() * 3 );
    size_t i = 0;
    while ( i < units.size() ) {
        const std::uint32_t u = units[ i ];
        if ( u >= 0xD800 && u <= 0xDBFF ) {
            if ( i + 1 < units.size() && units[ i + 1 ] >= 0xDC00 && units[ i + 1 ] <= 0xDFFF ) {
                const std::uint32_t cp
                    = 0x10000 + ( ( u - 0xD800 ) << 10 ) + ( units[ i + 1 ] - 0xDC00 );
                out.push_back( static_cast<char>( 0xF0 | ( cp >> 18 ) ) );
                out.push_back( static_cast<char>( 0x80 | ( ( cp >> 12 ) & 0x3F ) ) );
                out.push_back( static_cast<char>( 0x80 | ( ( cp >> 6 ) & 0x3F ) ) );
                out.push_back( static_cast<char>( 0x80 | ( cp & 0x3F ) ) );
                i += 2;
                continue;
            }
            out.push_back( '?' );
            ++i;
            continue;
        }
        if ( u >= 0xDC00 && u <= 0xDFFF ) {
            out.push_back( '?' );
            ++i;
            continue;
        }
        if ( u < 0x80 ) {
            out.push_back( static_cast<char>( u ) );
            ++i;
            continue;
        }
        if ( u < 0x800 ) {
            out.push_back( static_cast<char>( 0xC0 | ( u >> 6 ) ) );
            out.push_back( static_cast<char>( 0x80 | ( u & 0x3F ) ) );
        }
        else {
            out.push_back( static_cast<char>( 0xE0 | ( u >> 12 ) ) );
            out.push_back( static_cast<char>( 0x80 | ( ( u >> 6 ) & 0x3F ) ) );
            out.push_back( static_cast<char>( 0x80 | ( u & 0x3F ) ) );
        }
        ++i;
    }
    return out;
}

// Decode a chunk of UTF-8. Invalid sequences become U+FFFD rendered as the
// EF BF BD byte sequence, one replacement per offending byte, mirroring the
// reference fromUtf8 behaviour.
static std::string decodeUtf8( const char* data, int len )
{
    std::string out;
    out.reserve( static_cast<size_t>( len ) );
    const auto* input = reinterpret_cast<const unsigned char*>( data );
    int i = 0;
    while ( i < len ) {
        const unsigned char b = input[ i ];
        if ( b < 0x80 ) {
            out.push_back( static_cast<char>( b ) );
            ++i;
            continue;
        }

        int need = 0;
        std::uint32_t minValue = 0;
        if ( b >= 0xC2 && b <= 0xDF ) {
            need = 2;
            minValue = 0x80;
        }
        else if ( b >= 0xE0 && b <= 0xEF ) {
            need = 3;
            minValue = 0x800;
        }
        else if ( b >= 0xF0 && b <= 0xF4 ) {
            need = 4;
            minValue = 0x10000;
        }
        else {
            // Lone continuation bytes, C0/C1 overlong leads and invalid F5-FF
            // leads are all one-byte errors.
            out.append( "\xEF\xBF\xBD" );
            ++i;
            continue;
        }

        bool valid = i + need <= len;
        std::uint32_t value = 0;
        if ( valid ) {
            value = b & ( 0x7F >> need );
            for ( int j = 1; j < need; ++j ) {
                const unsigned char c = input[ i + j ];
                if ( ( c & 0xC0 ) != 0x80 ) {
                    valid = false;
                    break;
                }
                value = ( value << 6 ) | ( c & 0x3F );
            }
        }

        if ( valid
             && ( value < minValue || ( value >= 0xD800 && value <= 0xDFFF )
                  || value > 0x10FFFF ) ) {
            valid = false;
        }

        if ( !valid ) {
            out.append( "\xEF\xBF\xBD" );
            ++i;
        }
        else {
            out.append( data + i, static_cast<std::size_t>( need ) );
            i += need;
        }
    }
    return out;
}

// Decode a chunk of UTF-16LE/BE. Odd trailing bytes are dropped; invalid
// surrogate sequences yield '?' per position, with the survivor resynchronised
// at the following code unit.
static std::string decodeUtf16( const char* data, int len, EncodingKind kind )
{
    const int count = len / 2;
    std::vector<char16_t> units( static_cast<size_t>( count ) );
    std::memcpy( units.data(), data, static_cast<size_t>( count ) * 2 );

    if ( kind == EncodingKind::Utf16BE ) {
        for ( auto& unit : units ) {
            unit = static_cast<char16_t>( ( unit >> 8 ) | ( unit << 8 ) );
        }
    }

    std::string out;
    out.resize( static_cast<size_t>( count ) * 3 );

    size_t inPos = 0;
    size_t outPos = 0;
    while ( inPos < static_cast<size_t>( count ) ) {
        const auto res = simdutf::convert_utf16le_to_utf8_with_errors(
            units.data() + inPos, static_cast<size_t>( count ) - inPos, out.data() + outPos );
        if ( res.error == simdutf::SUCCESS ) {
            outPos += res.count;
            break;
        }

        outPos += simdutf::convert_valid_utf16_to_utf8( units.data() + inPos, res.count,
                                                        out.data() + outPos );
        out[ outPos++ ] = '?';
        inPos += res.count + 1;
    }

    out.resize( outPos );
    return out;
}

// Decode a chunk of UTF-32LE/BE. Odd trailing bytes are dropped; each 32-bit
// value becomes UTF-16 code units with the reference fromUcs4 arithmetic
// (including the skip of a leading byte order mark in either byte order).
static std::string decodeUtf32( const char* data, int len, EncodingKind kind )
{
    const int count = len / 4;
    const auto* input = reinterpret_cast<const unsigned char*>( data );

    std::vector<std::uint32_t> values( static_cast<size_t>( count ) );
    for ( int i = 0; i < count; ++i ) {
        const std::uint32_t b0 = input[ 4 * i ];
        const std::uint32_t b1 = input[ 4 * i + 1 ];
        const std::uint32_t b2 = input[ 4 * i + 2 ];
        const std::uint32_t b3 = input[ 4 * i + 3 ];
        values[ static_cast<size_t>( i ) ] = kind == EncodingKind::Utf32LE
                                                 ? ( b0 | ( b1 << 8 ) | ( b2 << 16 ) | ( b3 << 24 ) )
                                                 : ( ( b0 << 24 ) | ( b1 << 16 ) | ( b2 << 8 ) | b3 );
    }

    size_t firstValue = 0;
    if ( count > 0 ) {
        const auto* h = reinterpret_cast<const unsigned char*>( values.data() );
        if ( ( h[ 0 ] == 0x00 && h[ 1 ] == 0x00 && h[ 2 ] == 0xFE && h[ 3 ] == 0xFF )
             || ( h[ 0 ] == 0xFF && h[ 1 ] == 0xFE && h[ 2 ] == 0x00 && h[ 3 ] == 0x00 ) ) {
            firstValue = 1;
        }
    }

    std::vector<char16_t> units;
    units.reserve( static_cast<size_t>( count ) * 2 );
    for ( size_t i = firstValue; i < values.size(); ++i ) {
        const std::uint32_t value = values[ i ];
        if ( value < 0x10000 ) {
            units.push_back( static_cast<char16_t>( value ) );
        }
        else {
            units.push_back( static_cast<char16_t>( ( value >> 10 ) + 0xD7C0 ) );
            units.push_back( static_cast<char16_t>( ( value & 0x3FF ) + 0xDC00 ) );
        }
    }

    return unitsToUtf8( units );
}

// Decode a chunk of UTF encoding to UTF-8. `kind` is the concrete encoding
// (BOM already resolved). Surrogate handling follows the reference
// implementations: simdutf validation with '?' recovery for the 16-bit kinds,
// the fromUcs4 conversion (unchecked pair arithmetic) for the 32-bit kinds.
class UtfDecoder : public klogg::enc::AbstractDecoder {
public:
    explicit UtfDecoder( EncodingKind kind )
        : kind_( kind )
    {
    }

    std::string decode( const char* data, int len ) override
    {
        if ( len <= 0 ) {
            return {};
        }

        if ( bomPending_ ) {
            resolveEndianAndStripBom( data, len );
            bomPending_ = false;
        }

        const int shift = bomSkip_;
        bomSkip_ = 0;
        return decodeUtf( data + shift, len - shift );
    }

private:
    // Resolve the effective endianness and, if the first chunk begins with the
    // matching byte order mark, record how many bytes to skip.
    void resolveEndianAndStripBom( const char* data, int len )
    {
        switch ( kind_ ) {
        case EncodingKind::Utf8:
            if ( len >= 3 && data[ 0 ] == '\xEF' && data[ 1 ] == '\xBB' && data[ 2 ] == '\xBF' ) {
                bomSkip_ = 3;
            }
            break;
        case EncodingKind::Utf16LE:
            if ( len >= 2 && data[ 0 ] == '\xFF' && data[ 1 ] == '\xFE' ) {
                bomSkip_ = 2;
            }
            break;
        case EncodingKind::Utf16BE:
            if ( len >= 2 && data[ 0 ] == '\xFE' && data[ 1 ] == '\xFF' ) {
                bomSkip_ = 2;
            }
            break;
        case EncodingKind::Utf32LE:
            if ( len >= 4 && data[ 0 ] == '\xFF' && data[ 1 ] == '\xFE' && data[ 2 ] == '\x00'
                 && data[ 3 ] == '\x00' ) {
                bomSkip_ = 4;
            }
            break;
        case EncodingKind::Utf32BE:
            if ( len >= 4 && data[ 0 ] == '\x00' && data[ 1 ] == '\x00' && data[ 2 ] == '\xFE'
                 && data[ 3 ] == '\xFF' ) {
                bomSkip_ = 4;
            }
            break;
        case EncodingKind::Utf16:
            if ( len >= 2 && data[ 0 ] == '\xFF' && data[ 1 ] == '\xFE' ) {
                effective_ = EncodingKind::Utf16LE;
                bomSkip_ = 2;
            }
            else if ( len >= 2 && data[ 0 ] == '\xFE' && data[ 1 ] == '\xFF' ) {
                effective_ = EncodingKind::Utf16BE;
                bomSkip_ = 2;
            }
            else {
                // No BOM: assume big-endian (rarely reached).
                effective_ = EncodingKind::Utf16BE;
            }
            break;
        case EncodingKind::Utf32:
            if ( len >= 4 && data[ 0 ] == '\xFF' && data[ 1 ] == '\xFE' && data[ 2 ] == '\x00'
                 && data[ 3 ] == '\x00' ) {
                effective_ = EncodingKind::Utf32LE;
                bomSkip_ = 4;
            }
            else if ( len >= 4 && data[ 0 ] == '\x00' && data[ 1 ] == '\x00' && data[ 2 ] == '\xFE'
                      && data[ 3 ] == '\xFF' ) {
                effective_ = EncodingKind::Utf32BE;
                bomSkip_ = 4;
            }
            else {
                effective_ = EncodingKind::Utf32BE;
            }
            break;
        default:
            break;
        }
    }

    std::string decodeUtf( const char* data, int len ) const
    {
        const auto kind = effective_ == EncodingKind::Unknown ? kind_ : effective_;

        if ( kind == EncodingKind::Utf8 ) {
            return decodeUtf8( data, len );
        }

        if ( kind == EncodingKind::Utf16LE || kind == EncodingKind::Utf16BE ) {
            return decodeUtf16( data, len, kind );
        }

        if ( kind == EncodingKind::Utf32LE || kind == EncodingKind::Utf32BE ) {
            return decodeUtf32( data, len, kind );
        }

        return {};
    }

    EncodingKind kind_;
    EncodingKind effective_{ EncodingKind::Unknown };
    bool bomPending_{ true };
    int bomSkip_{ 0 };
};

} // namespace

EncodingParameters::EncodingParameters( const klogg::Encoding& encoding )
{
    isUtf8Compatible = encoding.mib == 106 || encoding.mib == 3;
    isUtf16LE = encoding.mib == 1014;

    switch ( encoding.kind ) {
    case EncodingKind::Utf16LE:
        lineFeedWidth = 2;
        lineFeedIndex = 0;
        break;
    case EncodingKind::Utf16BE:
        lineFeedWidth = 2;
        lineFeedIndex = 1;
        break;
    case EncodingKind::Utf32LE:
        lineFeedWidth = 4;
        lineFeedIndex = 0;
        break;
    case EncodingKind::Utf32BE:
        lineFeedWidth = 4;
        lineFeedIndex = 3;
        break;
    default:
        break;
    }
}

namespace klogg {
namespace enc {

std::shared_ptr<AbstractDecoder> makeDecoder( const Encoding& encoding )
{
    switch ( encoding.kind ) {
    case EncodingKind::Utf8:
    case EncodingKind::Utf16:
    case EncodingKind::Utf16LE:
    case EncodingKind::Utf16BE:
    case EncodingKind::Utf32:
    case EncodingKind::Utf32LE:
    case EncodingKind::Utf32BE:
        return std::make_shared<UtfDecoder>( encoding.kind );
    case EncodingKind::Legacy:
        return makeLegacyDecoder( encoding );
    case EncodingKind::Unknown:
    default:
        return nullptr;
    }
}

} // namespace enc
} // namespace klogg

std::optional<klogg::Encoding>
EncodingDetector::detectEncoding( const klogg::vector<char>& block ) const
{
    UniqueLock lock( mutex_ );
    return klogg::enc::detectFromBlock( block.data(), block.size() );
}

TextCodecHolder::TextCodecHolder( std::optional<klogg::Encoding> encoding )
    : encoding_{ encoding }
    , encodingParams_{ encoding ? EncodingParameters( *encoding ) : EncodingParameters{} }
{
}

std::optional<klogg::Encoding> TextCodecHolder::codec() const
{
    SharedLock guard( mutex_ );
    return encoding_;
}

EncodingParameters TextCodecHolder::encodingParameters() const
{
    SharedLock guard( mutex_ );
    return encodingParams_;
}

int TextCodecHolder::mibEnum() const
{
    SharedLock guard( mutex_ );
    return encoding_ ? encoding_->mib : 0;
}

void TextCodecHolder::setCodec( std::optional<klogg::Encoding> encoding )
{
    UniqueLock guard( mutex_ );
    encoding_ = encoding;
    encodingParams_ = encoding ? EncodingParameters( *encoding ) : EncodingParameters{};
}

TextDecoder TextCodecHolder::makeDecoder() const
{
    SharedLock guard( mutex_ );
    if ( encoding_ ) {
        return { klogg::enc::makeDecoder( *encoding_ ), encodingParams_ };
    }

    return { nullptr, encodingParams_ };
}
