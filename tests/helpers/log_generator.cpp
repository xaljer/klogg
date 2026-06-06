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

    std::cout << timeBuf << " [" << level << "] [thread-" << threadId << "] "
              << msg << std::endl;
}

static void printUsage( const char* prog )
{
    std::cerr << "Usage: " << prog << " [options]\n"
              << "  --lines N      Total lines to output (default: unlimited)\n"
              << "  --interval N   Interval in ms between lines (default: 100)\n"
              << "  --burst N      Burst size: output N lines then sleep (default: 1)\n";
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
