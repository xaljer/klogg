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

#ifndef ENCODINGDETECTOR_H
#define ENCODINGDETECTOR_H

#include "containers.h"
#include "encoding.h"
#include "synchronization.h"

#include <memory>
#include <optional>
#include <string>

namespace klogg {
namespace enc {

// Abstract streaming decoder. Implementations decode a chunk of raw bytes to
// UTF-8, keeping any state needed across chunk boundaries (BOM stripping,
// shift state for stateful encodings).
class AbstractDecoder {
public:
    virtual ~AbstractDecoder() = default;
    virtual std::string decode( const char* data, int len ) = 0;
};

// Build a decoder for the given encoding. UTF-* encodings are handled by an
// internal decoder that also strips the byte order mark; Legacy encodings are
// delegated to the platform decoder; Unknown yields nullptr.
std::shared_ptr<AbstractDecoder> makeDecoder( const Encoding& encoding );

} // namespace enc
} // namespace klogg

struct EncodingParameters {
    EncodingParameters() = default;
    explicit EncodingParameters( const klogg::Encoding& encoding );

    bool isUtf8Compatible{ false };
    bool isUtf16LE{ false };

    int lineFeedWidth{ 1 };
    int lineFeedIndex{ 0 };

    bool operator==( const EncodingParameters& other ) const
    {
        return lineFeedWidth == other.lineFeedWidth && lineFeedIndex == other.lineFeedIndex;
    }

    bool operator!=( const EncodingParameters& other ) const
    {
        return !operator==( other );
    }

    int getBeforeCrOffset() const
    {
        return lineFeedIndex;
    }

    int getAfterCrOffset() const
    {
        return lineFeedWidth - lineFeedIndex - 1;
    }
};

class EncodingDetector {
public:
    static EncodingDetector& getInstance()
    {
        static EncodingDetector instance;
        return instance;
    }

    EncodingDetector( const EncodingDetector& ) = delete;
    EncodingDetector& operator=( const EncodingDetector& ) = delete;
    EncodingDetector( const EncodingDetector&& ) = delete;
    EncodingDetector& operator=( const EncodingDetector&& ) = delete;

    std::optional<klogg::Encoding> detectEncoding( const klogg::vector<char>& block ) const;

private:
    EncodingDetector() = default;
    ~EncodingDetector() = default;

    mutable SharedMutex mutex_;
};

struct TextDecoder {
    std::shared_ptr<klogg::enc::AbstractDecoder> decoder;
    EncodingParameters encodingParams;

    std::string decodeBlock( const char* data, int len ) const
    {
        return decoder ? decoder->decode( data, len ) : std::string{};
    }
};

class TextCodecHolder {
public:
    explicit TextCodecHolder( std::optional<klogg::Encoding> encoding );

    void setCodec( std::optional<klogg::Encoding> encoding );

    std::optional<klogg::Encoding> codec() const;
    EncodingParameters encodingParameters() const;
    int mibEnum() const;

    TextDecoder makeDecoder() const;

private:
    std::optional<klogg::Encoding> encoding_;
    EncodingParameters encodingParams_;
    mutable SharedMutex mutex_;
};

#endif // ENCODINGDETECTOR_H
