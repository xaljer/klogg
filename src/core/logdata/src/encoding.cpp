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

#include "encoding.h"

#include <array>
#include <cstring>
#include <utility>

#include <uchardet.h>

#include "log.h"

namespace {

// One frozen registry row. `name` is the canonical name Qt reports for the
// MIB enum, and also the identity of the row, so two Encodings are equal iff
// their names match. The table is validated at build time by
// klogg_logdata_tests against the Qt codec list so it cannot
// drift from Qt's codec table. US-ASCII (MIB 3) is deliberately absent: Qt
// 6.7.3 returns a null codec for it, so absence is the faithful encoding of
// "no codec".
struct RegistryEntry {
    const char* name;
    klogg::EncodingKind kind;
    int mib;
    unsigned codepage; // Windows code page (0 = n/a)
    const char* iconv; // iconv name (nullptr = n/a)
};

// Codepage 0 / iconv nullptr mean "not decodeable through that path"; the
// decoders fall back to U+FFFD for the bytes they cannot handle.
constexpr std::array<RegistryEntry, 45> registry{ {
    // Unicode
    { "UTF-8", klogg::EncodingKind::Utf8, 106, 65001, "UTF-8" },
    { "UTF-16", klogg::EncodingKind::Utf16, 1015, 0, "UTF-16" },
    { "UTF-16BE", klogg::EncodingKind::Utf16BE, 1013, 1201, "UTF-16BE" },
    { "UTF-16LE", klogg::EncodingKind::Utf16LE, 1014, 1200, "UTF-16LE" },
    { "UTF-32", klogg::EncodingKind::Utf32, 1017, 0, "UTF-32" },
    { "UTF-32BE", klogg::EncodingKind::Utf32BE, 1018, 12001, "UTF-32BE" },
    { "UTF-32LE", klogg::EncodingKind::Utf32LE, 1019, 12000, "UTF-32LE" },

    // ISO-8859 family (single byte)
    { "ISO-8859-1", klogg::EncodingKind::Legacy, 4, 28591, "ISO-8859-1" },
    { "ISO-8859-2", klogg::EncodingKind::Legacy, 5, 28592, "ISO-8859-2" },
    { "ISO-8859-3", klogg::EncodingKind::Legacy, 6, 28593, "ISO-8859-3" },
    { "ISO-8859-4", klogg::EncodingKind::Legacy, 7, 28594, "ISO-8859-4" },
    { "ISO-8859-5", klogg::EncodingKind::Legacy, 8, 28595, "ISO-8859-5" },
    { "ISO-8859-6", klogg::EncodingKind::Legacy, 82, 28596, "ISO-8859-6" },
    { "ISO-8859-7", klogg::EncodingKind::Legacy, 10, 28597, "ISO-8859-7" },
    { "ISO-8859-8", klogg::EncodingKind::Legacy, 85, 28598, "ISO-8859-8" },
    { "ISO-8859-9", klogg::EncodingKind::Legacy, 12, 28599, "ISO-8859-9" },
    { "ISO-8859-10", klogg::EncodingKind::Legacy, 13, 28602, "ISO-8859-10" },
    { "ISO-8859-13", klogg::EncodingKind::Legacy, 109, 28603, "ISO-8859-13" },
    { "ISO-8859-14", klogg::EncodingKind::Legacy, 110, 28604, "ISO-8859-14" },
    { "ISO-8859-15", klogg::EncodingKind::Legacy, 111, 28605, "ISO-8859-15" },
    { "ISO-8859-16", klogg::EncodingKind::Legacy, 112, 28606, "ISO-8859-16" },

    // Windows code pages (single byte)
    { "windows-1250", klogg::EncodingKind::Legacy, 2250, 1250, "WINDOWS-1250" },
    { "windows-1251", klogg::EncodingKind::Legacy, 2251, 1251, "WINDOWS-1251" },
    { "windows-1252", klogg::EncodingKind::Legacy, 2252, 1252, "WINDOWS-1252" },
    { "windows-1253", klogg::EncodingKind::Legacy, 2253, 1253, "WINDOWS-1253" },
    { "windows-1254", klogg::EncodingKind::Legacy, 2254, 1254, "WINDOWS-1254" },
    { "windows-1255", klogg::EncodingKind::Legacy, 2255, 1255, "WINDOWS-1255" },
    { "windows-1256", klogg::EncodingKind::Legacy, 2256, 1256, "WINDOWS-1256" },
    { "windows-1257", klogg::EncodingKind::Legacy, 2257, 1257, "WINDOWS-1257" },
    { "windows-1258", klogg::EncodingKind::Legacy, 2258, 1258, "WINDOWS-1258" },

    // Cyrillic / compatibility code pages
    { "KOI8-R", klogg::EncodingKind::Legacy, 2084, 20866, "KOI8-R" },
    { "KOI8-U", klogg::EncodingKind::Legacy, 2088, 21866, "KOI8-U" },
    { "macintosh", klogg::EncodingKind::Legacy, 2027, 10000, "macintosh" },
    { "IBM866", klogg::EncodingKind::Legacy, 2086, 866, "IBM866" },
    { "IBM850", klogg::EncodingKind::Legacy, 2009, 850, "IBM850" },

    // CJK double byte / multi byte
    { "GB2312", klogg::EncodingKind::Legacy, 2025, 936, "GB2312" },
    { "GBK", klogg::EncodingKind::Legacy, 113, 936, "GBK" },
    { "GB18030", klogg::EncodingKind::Legacy, 114, 54936, "GB18030" },
    { "Big5", klogg::EncodingKind::Legacy, 2026, 950, "Big5" },
    { "EUC-KR", klogg::EncodingKind::Legacy, 38, 51949, "EUC-KR" },
    { "windows-949", klogg::EncodingKind::Legacy, -949, 949, "CP949" },
    { "Shift_JIS", klogg::EncodingKind::Legacy, 17, 932, "SHIFT_JIS" },
    { "EUC-JP", klogg::EncodingKind::Legacy, 18, 51932, "EUC-JP" },
    { "ISO-2022-JP", klogg::EncodingKind::Legacy, 39, 50220, "ISO-2022-JP" },

    // Thai
    { "TIS-620", klogg::EncodingKind::Legacy, 2259, 874, "TIS-620" },
} };

// Aliases accepted by fromName and normalised by fromUchardetName. Keyed by
// the upper-cased spelling, value is the canonical registry name. US-ASCII
// and its spellings are intentionally absent (Qt 6.7.3 has no US-ASCII codec).
struct AliasEntry {
    const char* alias;
    const char* canonical;
};

constexpr std::array<AliasEntry, 27> aliases{ {
    { "UTF8", "UTF-8" },
    { "UTF-16-LITTLE", "UTF-16LE" },
    { "UTF-16-BIG-ENDIAN", "UTF-16BE" },
    { "UTF-16", "UTF-16" },
    { "UTF-32", "UTF-32" },
    { "UTF-32LE", "UTF-32LE" },
    { "UTF-32BE", "UTF-32BE" },
    { "LATIN1", "ISO-8859-1" },
    { "LATIN-1", "ISO-8859-1" },
    { "ISO8859-1", "ISO-8859-1" },
    { "ISO8859-15", "ISO-8859-15" },
    { "ISO-8859-11", "TIS-620" },
    { "WINDOWS-1250", "windows-1250" },
    { "WINDOWS-1251", "windows-1251" },
    { "WINDOWS-1252", "windows-1252" },
    { "WINDOWS-1253", "windows-1253" },
    { "WINDOWS-1254", "windows-1254" },
    { "WINDOWS-1255", "windows-1255" },
    { "WINDOWS-1256", "windows-1256" },
    { "WINDOWS-1257", "windows-1257" },
    { "WINDOWS-1258", "windows-1258" },
    { "CP1251", "windows-1251" },
    { "CP1252", "windows-1252" },
    { "CP949", "windows-949" },
    { "BIG5", "Big5" },
    { "BIG-5", "Big5" },
    { "SHIFT-JIS", "Shift_JIS" },
} };

std::string toUpper( const char* s )
{
    std::string out;
    out.reserve( std::strlen( s ) );
    for ( ; *s != '\0'; ++s ) {
        const auto c = static_cast<unsigned char>( *s );
        out.push_back( static_cast<char>( ( c >= 'a' && c <= 'z' ) ? c - 'a' + 'A' : c ) );
    }
    return out;
}

klogg::Encoding makeEncoding( const RegistryEntry& entry )
{
    return klogg::Encoding{ entry.name, entry.kind, entry.mib, entry.codepage,
                            entry.iconv ? entry.iconv : std::string{} };
}

} // namespace

namespace klogg {
namespace enc {

std::optional<Encoding> fromName( const std::string& name )
{
    if ( name.empty() ) {
        return std::nullopt;
    }

    const auto upper = toUpper( name.c_str() );

    // Direct canonical match first (case-insensitive, so "Windows-1252" hits).
    for ( const auto& entry : registry ) {
        if ( toUpper( entry.name ) == upper ) {
            return makeEncoding( entry );
        }
    }

    // Alias match: find the first registry entry whose canonical name equals
    // the alias's target.
    for ( const auto& alias : aliases ) {
        if ( toUpper( alias.alias ) == upper ) {
            for ( const auto& entry : registry ) {
                if ( std::strcmp( entry.name, alias.canonical ) == 0 ) {
                    return makeEncoding( entry );
                }
            }
        }
    }

    return std::nullopt;
}

std::optional<Encoding> fromMib( int mib )
{
    for ( const auto& entry : registry ) {
        if ( entry.mib == mib ) {
            return makeEncoding( entry );
        }
    }
    return std::nullopt;
}

std::optional<Encoding> fromUchardetName( const char* name )
{
    if ( name == nullptr || *name == '\0' ) {
        return std::nullopt;
    }
    return fromName( name );
}

std::optional<Encoding> defaultEncoding()
{
    return fromName( "ISO-8859-1" );
}

std::optional<Encoding> detectFromBlock( const char* data, size_t len )
{
    // Byte order marks win over every heuristic, matching the old
    // codecForUtfText precedence. Order matters: a UTF-32 LE BOM
    // (FF FE 00 00) starts with the UTF-16 LE prefix, so check 32-bit first.
    if ( len >= 4 && data[ 0 ] == '\xFF' && data[ 1 ] == '\xFE' && data[ 2 ] == '\x00'
         && data[ 3 ] == '\x00' ) {
        return fromMib( 1019 ); // UTF-32LE
    }
    if ( len >= 4 && data[ 0 ] == '\x00' && data[ 1 ] == '\x00' && data[ 2 ] == '\xFE'
         && data[ 3 ] == '\xFF' ) {
        return fromMib( 1018 ); // UTF-32BE
    }
    if ( len >= 3 && data[ 0 ] == '\xEF' && data[ 1 ] == '\xBB' && data[ 2 ] == '\xBF' ) {
        return fromMib( 106 ); // UTF-8
    }
    if ( len >= 2 && data[ 0 ] == '\xFF' && data[ 1 ] == '\xFE' ) {
        return fromMib( 1014 ); // UTF-16LE
    }
    if ( len >= 2 && data[ 0 ] == '\xFE' && data[ 1 ] == '\xFF' ) {
        return fromMib( 1013 ); // UTF-16BE
    }

    // No BOM: let uchardet guess.
    auto* detector = uchardet_new();
    if ( detector != nullptr ) {
        const auto rc = uchardet_handle_data( detector, data, len );
        if ( rc == 0 ) {
            uchardet_data_end( detector );
        }
        if ( rc == 0 ) {
            const auto* guess = uchardet_get_charset( detector );
            LOG_DEBUG << "Uchardet encoding guess " << guess;
            const auto result = fromUchardetName( guess );
            uchardet_delete( detector );
            if ( result.has_value() ) {
                return result;
            }
        }
        else {
            uchardet_delete( detector );
        }
    }

    // Last resort: ISO-8859-1.
    return defaultEncoding();
}

} // namespace enc
} // namespace klogg
