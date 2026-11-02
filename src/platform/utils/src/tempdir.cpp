#include "tempdir.h"

#include <filesystem>
#include <system_error>

#include "log.h"
#include "paths.h"

namespace klogg {

namespace {

// UTF-8 generic format (forward slashes on every platform) is the interface;
// generic_u8string() returns char8_t under C++20, so rewrap the bytes into
// std::string without any locale conversion.
std::string toUtf8( const std::filesystem::path& path )
{
    const auto u8 = path.generic_u8string();
    return std::string( reinterpret_cast<const char*>( u8.data() ), u8.size() );
}

bool ensureUsable( const std::string& dir )
{
    std::error_code ec;
    const auto path = platform::paths::pathFromUtf8( dir );

    std::filesystem::create_directories( path, ec );
    if ( ec ) {
        return false;
    }

    const auto status = std::filesystem::status( path, ec );
    if ( ec || !std::filesystem::is_directory( status ) ) {
        return false;
    }

    const auto permissions = status.permissions();
    return ( permissions
             & ( std::filesystem::perms::owner_write | std::filesystem::perms::group_write
                 | std::filesystem::perms::others_write ) )
           != std::filesystem::perms::none;
}

} // namespace

std::string resolveTempRoot( const std::string& customDir, const std::string& systemTempDir )
{
    const std::string base = customDir.empty() ? systemTempDir : customDir;

    const auto root = platform::paths::pathFromUtf8( base ) / "klogg_temp";
    if ( ensureUsable( toUtf8( root ) ) ) {
        return toUtf8( root );
    }

    if ( !customDir.empty() ) {
        LOG_WARNING << "Custom temp folder unusable, falling back to system temp: " << customDir;
    }

    const auto fallback = platform::paths::pathFromUtf8( systemTempDir ) / "klogg_temp";
    ensureUsable( toUtf8( fallback ) );
    return toUtf8( fallback );
}

} // namespace klogg
