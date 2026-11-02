/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
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

#ifndef KLOGG_UUID_H
#define KLOGG_UUID_H

#include <array>
#include <cstdint>
#include <random>
#include <string>

inline std::string generateIdFromUuid()
{
    std::array<uint8_t, 16> bytes{};
    std::random_device randomDevice;
    for ( auto& byte : bytes ) {
        byte = static_cast<uint8_t>( randomDevice() );
    }

    static constexpr char hexDigits[] = "0123456789abcdef";
    std::string result;
    result.reserve( 36 );
    for ( size_t i = 0; i < bytes.size(); ++i ) {
        if ( i == 4 || i == 6 || i == 8 || i == 10 ) {
            result += '-';
        }
        result += hexDigits[ bytes[ i ] >> 4 ];
        result += hexDigits[ bytes[ i ] & 0x0f ];
    }
    return result;
}

#endif // KLOGG_UUID_H
