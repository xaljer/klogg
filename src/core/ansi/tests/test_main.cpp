#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

// The ANSI module is Qt-free, so the suite needs nothing beyond Catch2.
int main( int argc, char* argv[] )
{
    return Catch::Session().run( argc, argv );
}
