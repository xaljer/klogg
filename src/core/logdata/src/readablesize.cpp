/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2020 Anton Filimonov and other contributors
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

#include <array>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <tuple>

#include "readablesize.h"

std::string readableSize( uint64_t size )
{
    static const std::array<std::tuple<uint64_t, uint64_t, const char*>, 4> sizeSteps
        = { std::make_tuple( 0x10000000000ull, 30ull, "TiB" ),
            std::make_tuple( 0x40000000ull, 20ull, "GiB" ),
            std::make_tuple( 0x100000ull, 10ull, "MiB" ),
            std::make_tuple( 0x400ull, 0ull, "KiB" ) };

    std::ostringstream output;

    const auto sizeStep
        = std::find_if( sizeSteps.cbegin(), sizeSteps.cend(),
                        [ &size ]( const auto& step ) { return size >= std::get<0>( step ); } );

    if ( sizeStep != sizeSteps.end() ) {
        const auto humanSize = static_cast<double>( ( size >> std::get<1>( *sizeStep ) ) ) / 1024.0;
        output << std::fixed << std::setprecision( 2 ) << humanSize << " "
               << std::get<2>( *sizeStep );
    }
    else {
        output << size << " B";
    }
    return output.str();
}
