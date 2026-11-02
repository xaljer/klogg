/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
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

#include <QCoreApplication>

#include <mimalloc.h>

#include "configuration.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "linetypes_qt.h"
#include "logger.h"
#include "persistentinfo.h"

#include "cli.h"

const bool PersistentInfo::ForcePortable = true;

int main( int argc, char* argv[] )
{
#ifdef KLOGG_USE_MIMALLOC
    mi_stats_reset();
#endif
    qRegisterMetaType<LinesCount>( "LinesCount" );
    qRegisterMetaType<LineNumber>( "LineNumber" );

    QCoreApplication app( argc, argv );
    CliParameters parameters( app, true );

    logging::enableLogging( true, static_cast<logging::LogLevel>( parameters.log_level ) );

    // Own composition root: this binary creates no Core API session, so it
    // initializes the platform services the engine depends on itself (it must
    // also keep working without an engine session bringing them up).
    // initialize() must precede the first store access below.
    PersistentInfo::initialize();

    auto configuration = Configuration::getSynced();

    // Completion arrives via a queued post, already on the main thread.
    const auto postTarget = []( std::function<void()> task ) {
        QMetaObject::invokeMethod( QCoreApplication::instance(), std::move( task ),
                                   Qt::QueuedConnection );
    };
    LogFilteredData* filteredDataPtr = nullptr;
    auto optionsHolder = std::make_shared<EngineOptionsHolder>();
    optionsHolder->set( configuration.engineOptions() );

    // The callbacks struct has more members than this tool uses; assign the one
    // it needs instead of leaving the rest implicit in an aggregate initializer.
    LogDataCallbacks callbacks;
    callbacks.onLoadingFinished = [ &filteredDataPtr, &parameters ]( LoadingStatus ) {
        filteredDataPtr->runSearch(
            RegularExpressionPattern( parameters.pattern.toStdString() ) );
    };

    LogData logData{ postTarget, callbacks, optionsHolder };
    auto filteredData = logData.getNewFilteredData();
    filteredDataPtr = filteredData.get();

    filteredData->addMatchesSearchProgressedListener(
        [ & ]( LinesCount nbMatches, int progress, LineNumber ) {
            if ( progress == 100 ) {

                LOG_INFO << "Searched finished, got " << nbMatches.get() << " matches";

                const auto defaultChunkSize = 1000_lcount;
                for ( auto chunkStart = 0_lnum; chunkStart < nbMatches;
                      chunkStart = chunkStart + defaultChunkSize ) {
                    auto chunkSize
                        = std::min( defaultChunkSize.get(), nbMatches.get() - chunkStart.get() );
                    auto lines = filteredData->getLines( chunkStart, LinesCount( chunkSize ) );
                    for ( const auto& l : lines ) {
                        std::cout << l << "\n";
                    }
                }

                exit( EXIT_SUCCESS );
            }
        } );

    logData.attachFile( parameters.filenames.front().toStdString() );
    return app.exec();
}