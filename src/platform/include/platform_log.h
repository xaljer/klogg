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

#ifndef KLOGG_PLATFORM_LOG_H
#define KLOGG_PLATFORM_LOG_H

#include <chrono>
#include <filesystem>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>

namespace platform::log {

enum class Level { Debug = 0, Info = 1, Warning = 2, Error = 3 };

// Process-global sink configuration, set once by the composition root before
// any module logs. Both sinks share one aligned format and timestamp source.
void setLevel( Level level );
void setConsoleEnabled( bool enabled );
// Opens (and rotates when oversized) the file sink. Returns false when the
// file cannot be opened; the previous sink is left untouched in that case.
bool setFile( std::filesystem::path path );

bool enabled( Level level );
std::string filePath();

// Writes an already-formatted message (e.g. bridged from another logging
// framework) through the same sink and format.
void write( Level level, const std::string& message );

// Flush the file sink (e.g. before a crash dump); safe to call often.
void flush();

namespace detail {

template <typename T, std::enable_if_t<!std::is_enum_v<T>, int> = 0>
void append( std::ostream& os, const T& value )
{
    os << value;
}

inline void append( std::ostream& os, const std::chrono::microseconds& duration )
{
    os << static_cast<float>( duration.count() ) / 1000.f << " ms";
}

template <typename E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
void append( std::ostream& os, E value )
{
    os << static_cast<std::underlying_type_t<E>>( value );
}

template <typename T> void append( std::ostream& os, const std::optional<T>& value )
{
    if ( value ) {
        append( os, *value );
    }
    else {
        os << "<empty>";
    }
}

} // namespace detail

// Accumulates one log line and commits it to the sink on destruction. Created
// by the LOG_* macros; the level check happens before construction, so a
// disabled log line costs nothing.
class Record {
  public:
    Record( Level level, const char* function, int line );
    ~Record();

    Record( const Record& ) = delete;
    Record& operator=( const Record& ) = delete;

    std::ostream& stream()
    {
        return buffer_;
    }

    template <typename T> Record& operator<<( const T& value )
    {
        detail::append( buffer_, value );
        return *this;
    }

  private:
    Level level_;
    const char* function_;
    int line_;
    std::ostringstream buffer_;
};

} // namespace platform::log

#define KLOGG_LOG_IF( level )                                                                      \
    if ( !::platform::log::enabled( level ) ) {                                                    \
        ;                                                                                          \
    }                                                                                              \
    else                                                                                           \
        ::platform::log::Record( level, __func__, __LINE__ ).stream()

#define LOG_DEBUG KLOGG_LOG_IF( ::platform::log::Level::Debug )
#define LOG_INFO KLOGG_LOG_IF( ::platform::log::Level::Info )
#define LOG_WARNING KLOGG_LOG_IF( ::platform::log::Level::Warning )
#define LOG_ERROR KLOGG_LOG_IF( ::platform::log::Level::Error )

#endif // KLOGG_PLATFORM_LOG_H
