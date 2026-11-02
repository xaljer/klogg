#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include <QApplication>

#include <logger.h>

#include "configuration.h"
#include <persistentinfo.h>

const bool PersistentInfo::ForcePortable = true;

// The UI suite reaches into the Qt shell (quickfind, UI-side user data, recorder
// sessions), so it builds the same composition root the application does.
int main( int argc, char* argv[] )
{
    QApplication app( argc, argv );

    logging::enableLogging();

    // Composition root: the store location has to be selected before the first
    // store access, which Configuration::getSynced() performs
    PersistentInfo::initialize();
    Configuration::getSynced();

    return Catch::Session().run( argc, argv );
}
