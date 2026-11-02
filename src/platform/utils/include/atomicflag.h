/*
 * Copyright (C) 2009, 2010, 2013, 2014, 2015 Nicolas Bonnefon and other
 * contributors
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

#ifndef KLOGG_ATOMICFLAG_H
#define KLOGG_ATOMICFLAG_H

#include <atomic>

class AtomicFlag {
public:
    explicit AtomicFlag( bool initialState = false )
        : flag_( initialState )
    {
    }

    inline void set()
    {
        flag_.store( true, std::memory_order_release );
    }

    inline void clear()
    {
        flag_.store( false, std::memory_order_release );
    }

    inline explicit operator bool() const
    {
        return flag_.load( std::memory_order_acquire );
    }

    inline bool operator!() const
    {
        return !flag_.load( std::memory_order_acquire );
    }

private:
    std::atomic<bool> flag_;
};

#endif // ATOMICFLAG_H
