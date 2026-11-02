#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include <QCoreApplication>

#include <logger.h>

// Event visualization works on Qt string types and temporary files; a
// QCoreApplication is enough, no widgets are involved.
int main( int argc, char* argv[] )
{
    QCoreApplication app( argc, argv );
    logging::enableLogging();
    return Catch::Session().run( argc, argv );
}
