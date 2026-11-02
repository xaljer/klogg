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

#ifndef KLOGG_ENCODING_H
#define KLOGG_ENCODING_H

#include <optional>
#include <string>
#include <string_view>

namespace klogg {

// Broad family of a text encoding, used to pick a decode strategy. Legacy
// covers the single/multi-byte code pages handed to a system decoder
// (MultiByteToWideChar on Windows, iconv elsewhere). Utf16 / Utf32 are the
// generic byte-order-mark sniffing codecs, kept for name/MIB resolution and
// decoded like their LE/BE siblings once the BOM has chosen an endianness.
enum class EncodingKind { Unknown, Utf8, Utf16, Utf16LE, Utf16BE, Utf32, Utf32LE, Utf32BE, Legacy };

// Value type describing a text encoding. `name` is the canonical IANA name
// (e.g. "UTF-8", "ISO-8859-1") and also the identity key of the registry, so
// two Encodings describe the same thing iff their names match. A default
// constructed Encoding is not a legal value: absence of an encoding is
// expressed with std::optional<Encoding> (mirroring the nullptr
// that used to stand for "no codec").
struct Encoding {
    std::string name;
    EncodingKind kind{ EncodingKind::Unknown };
    int mib{ 0 };
    unsigned codepage{ 0 };
    std::string iconv;
};

namespace enc {

// Look up a registry entry by canonical name, any accepted alias, or the case
// used by uchardet. Returns std::nullopt when the name is unknown.
std::optional<Encoding> fromName( const std::string& name );
std::optional<Encoding> fromUchardetName( const char* name );

// Look up a registry entry by IANA MIB enum.
std::optional<Encoding> fromMib( int mib );

// The encoding used when nothing else is known: ISO-8859-1.
std::optional<Encoding> defaultEncoding();

// Detect the encoding of a block of bytes. Checks the byte order mark first,
// then falls back to a uchardet guess, then to defaultEncoding().
std::optional<Encoding> detectFromBlock( const char* data, size_t len );

} // namespace enc
} // namespace klogg

#endif // KLOGG_ENCODING_H
