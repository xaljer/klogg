#ifndef KLOGG_COLOR_H
#define KLOGG_COLOR_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace klogg {

// RGBA color. A default-constructed value (alpha 0) is the invalid/unset
// sentinel, mirroring QColor() semantics.
struct Rgba {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;

    constexpr Rgba() = default;
    constexpr Rgba( uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255 )
        : r( red )
        , g( green )
        , b( blue )
        , a( alpha )
    {
    }

    constexpr bool operator==( const Rgba& ) const = default;

    // "#aarrggbb" (alpha first, matching the API convention); "" when invalid
    std::string toHex() const
    {
        static constexpr char kHex[] = "0123456789abcdef";
        if ( a == 0 ) {
            return {};
        }
        std::string hex = "#00000000";
        auto writeByte = [ &hex ]( size_t offset, uint8_t value ) {
            hex[ offset ] = kHex[ value >> 4 ];
            hex[ offset + 1 ] = kHex[ value & 0xF ];
        };
        writeByte( 1, a );
        writeByte( 3, r );
        writeByte( 5, g );
        writeByte( 7, b );
        return hex;
    }

    // Parses "#aarrggbb"; nullopt on any malformed input
    static std::optional<Rgba> fromHex( std::string_view hex )
    {
        if ( hex.size() != 9 || hex[ 0 ] != '#' ) {
            return std::nullopt;
        }
        auto nibble = []( char c ) -> int {
            if ( c >= '0' && c <= '9' ) {
                return c - '0';
            }
            if ( c >= 'a' && c <= 'f' ) {
                return c - 'a' + 10;
            }
            if ( c >= 'A' && c <= 'F' ) {
                return c - 'A' + 10;
            }
            return -1;
        };
        uint8_t channels[ 4 ] = {};
        for ( size_t i = 0; i < 4; ++i ) {
            const int high = nibble( hex[ 1 + 2 * i ] );
            const int low = nibble( hex[ 2 + 2 * i ] );
            if ( high < 0 || low < 0 ) {
                return std::nullopt;
            }
            channels[ i ] = static_cast<uint8_t>( high * 16 + low );
        }
        return Rgba( channels[ 1 ], channels[ 2 ], channels[ 3 ], channels[ 0 ] );
    }
};

} // namespace klogg

#endif // KLOGG_COLOR_H
