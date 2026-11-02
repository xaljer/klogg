#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

// The regex module and this suite are Qt-free; the QRegularExpression
// comparison lives in qt_parity_test.cpp, which only builds on the Qt line.
int main( int argc, char* argv[] )
{
    return Catch::Session().run( argc, argv );
}
