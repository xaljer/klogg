#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

static const std::vector<std::string> kLevels
    = { "DEBUG", "INFO", "WARN", "ERROR", "FATAL" };
static const std::vector<std::string> kMessages
    = { "Request processed successfully",
        "Connection established to backend",
        "Cache miss for key {}",
        "User authentication completed",
        "File upload started",
        "Memory usage: 67%",
        "GC pause: 12ms",
        "Thread pool expanded to 8 workers",
        "Retry attempt 3 of 5",
        "Configuration reloaded from disk",
        "Health check passed",
        "Database query took 342ms",
        "Rate limit approached: 95/100",
        "TLS handshake completed in 4ms",
        "Batch processing: 1423 items/s" };

static const std::vector<int> kAnsiForegrounds
    = { 31, 32, 33, 34, 35, 36, 91, 92, 93, 94, 95, 96 };

static bool g_ansi = false;
static bool g_ansiFull = false;

static const char* ansiReset()
{
    return g_ansi ? "\033[0m" : "";
}

static const char* ansiLevelColor( const std::string& level )
{
    if ( !g_ansi ) {
        return "";
    }
    if ( level == "DEBUG" ) {
        return "\033[36m";
    }
    if ( level == "INFO" ) {
        return "\033[32m";
    }
    if ( level == "WARN" ) {
        return "\033[33m";
    }
    if ( level == "ERROR" ) {
        return "\033[31m";
    }
    if ( level == "FATAL" ) {
        return "\033[1;31m";
    }
    return "";
}

static std::string ansiRandomColor()
{
    if ( !g_ansiFull ) {
        return "";
    }
    const int code = kAnsiForegrounds[ rand() % kAnsiForegrounds.size() ];
    char buf[ 16 ];
    std::snprintf( buf, sizeof( buf ), "\033[%dm", code );
    return buf;
}

static std::string colorizeMessage( const std::string& msg )
{
    if ( !g_ansiFull ) {
        return msg;
    }
    std::ostringstream oss;
    std::istringstream iss( msg );
    std::string word;
    bool first = true;
    while ( iss >> word ) {
        if ( !first ) {
            oss << ' ';
        }
        first = false;
        if ( rand() % 3 == 0 ) {
            oss << ansiRandomColor() << word << ansiReset();
        }
        else {
            oss << word;
        }
    }
    return oss.str();
}

static void printLine()
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t( now );
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch() )
              % 1000;

    struct tm local_tm;
#ifdef _WIN32
    localtime_s( &local_tm, &time_t_now );
#else
    localtime_r( &time_t_now, &local_tm );
#endif

    char timeBuf[ 32 ];
    std::snprintf( timeBuf, sizeof( timeBuf ), "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
                   local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday,
                   local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec, ms.count() );

    const auto& level = kLevels[ rand() % kLevels.size() ];
    const auto& msg = kMessages[ rand() % kMessages.size() ];
    int threadId = ( rand() % 32 ) + 1;

    std::cout << timeBuf << " " << ansiLevelColor( level ) << "[" << level << "]"
              << ansiReset() << " [thread-" << threadId << "] "
              << colorizeMessage( msg ) << std::endl;
}

static void printUsage( const char* prog )
{
    std::cerr << "Usage: " << prog << " [options]\n"
              << "  --lines N      Total lines to output (default: unlimited)\n"
              << "  --interval N   Interval in ms between lines (default: 100)\n"
              << "  --burst N      Burst size: output N lines then sleep (default: 1)\n"
              << "  --ansi         Add ANSI color codes to severity levels\n"
              << "  --ansi-full    Full ANSI: level colors + random mid-line colors\n";
}

int main( int argc, char* argv[] )
{
    int totalLines = -1;
    int intervalMs = 100;
    int burstSize = 1;

    for ( int i = 1; i < argc; ++i ) {
        std::string arg( argv[ i ] );
        if ( arg == "--lines" && i + 1 < argc ) {
            totalLines = std::atoi( argv[ ++i ] );
        }
        else if ( arg == "--interval" && i + 1 < argc ) {
            intervalMs = std::atoi( argv[ ++i ] );
        }
        else if ( arg == "--burst" && i + 1 < argc ) {
            burstSize = std::atoi( argv[ ++i ] );
        }
        else if ( arg == "--ansi" ) {
            g_ansi = true;
        }
        else if ( arg == "--ansi-full" ) {
            g_ansi = true;
            g_ansiFull = true;
        }
        else {
            printUsage( argv[ 0 ] );
            return 1;
        }
    }

    srand( static_cast<unsigned>( time( nullptr ) ) );

    int count = 0;
    while ( totalLines < 0 || count < totalLines ) {
        for ( int b = 0; b < burstSize && ( totalLines < 0 || count < totalLines );
              ++b, ++count ) {
            printLine();
        }

        if ( intervalMs > 0
             && ( totalLines < 0 || count + burstSize - 1 < totalLines ) ) {
            std::this_thread::sleep_for( std::chrono::milliseconds( intervalMs ) );
        }
    }

    return 0;
}
