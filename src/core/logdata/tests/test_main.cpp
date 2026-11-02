#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include <logger.h>

// The log data structures and their tests are Qt-free; no application object is
// needed to drive them.
int main( int argc, char* argv[] )
{
    logging::enableLogging();
    return Catch::Session().run( argc, argv );
}
