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

// Perfetto repro driver for the post-B8 UI stutter investigation
// (docs/architecture/perf-stutter-notes.md). It replays the four manual-test
// symptoms on a big log file:
//   phase "resize"   - 20 splitter drag steps between main/filtered views
//   phase "search"   - click Search, filtered view switches to the new search
//   phase "tabback"  - switch to another tab and back to the big file
// Every phase is bracketed by "ui"-category instant markers so the trace can
// be sliced by wall-clock window. Runs only when KLOGG_PERF_REPRO_FILE points
// at the big log; otherwise it skips. Pair it with KLOGG_PERFETTO_TRACE:
//   QT_QPA_PLATFORM=offscreen
//   KLOGG_PERFETTO_TRACE=$PWD/repro.pftrace
//   KLOGG_PERF_REPRO_FILE=D:/workspace/klogg/build/biglog_manual_test.log
//   klogg_itests "[perf_repro]"

#include <chrono>

#include <catch2/catch.hpp>

#include <QEventLoop>
#include <QFileInfo>
#include <QSignalSpy>
#include <QSplitter>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>

#include "test_utils.h"

#include "klogg/api.h"

#include "apisession.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "log.h"
#include "log_qt.h"
#include "mainwindow.h"
#include "session.h"
#include "tabbedcrawlerwidget.h"
#include "uitracing.h"

namespace {

struct PerfReproAccess;

// Perfetto event names must be static strings, so the marker is a macro:
// the literal stays at the call site. Instant marker first, wall-clock log
// second: the marker is what the trace slicing keys on
#define PHASE_MARKER( name )                                                                       \
    do {                                                                                           \
        KLOGG_UI_TRACE_INSTANT( name );                                                            \
        LOG_WARNING << "perf_repro phase: " name;                                                  \
    } while ( false )

} // namespace

template <>
struct CrawlerWidget::access_by<PerfReproAccess> {
    // Non-owning: the crawler is owned by the MainWindow tab widget
    CrawlerWidget* crawler = nullptr;

    bool isLoadingFinished() const
    {
        return !crawler->loadingInProgress_;
    }

    LinesCount mainLines() const
    {
        return static_cast<const AbstractLogView*>( crawler->logMainView_ )->getNbLine();
    }

    LinesCount filteredLines() const
    {
        return static_cast<const AbstractLogView*>( crawler->filteredView_ )->getNbLine();
    }

    int height() const
    {
        return crawler->height();
    }

    // Drag the main/filtered splitter handle (moveSplitter is protected on
    // QSplitter; a friend of CrawlerWidget reaches it through the crawler)
    void dragSplitter( int pos )
    {
        crawler->moveSplitter( pos, 1 );
    }

    // Click Search and wait for the fresh SearchId to reach 100% on the API
    void runSearchAndWait( const QString& pattern )
    {
        const auto previousSearch = crawler->currentSearchId_;
        crawler->searchLineEdit_->clearEditText();
        QTest::keyClicks( crawler->searchLineEdit_, pattern );
        PHASE_MARKER( "search:click" );
        QTest::mouseClick( crawler->searchButton_, Qt::LeftButton );

        waitUiState(
            [ & ] {
                const auto& current = crawler->currentSearchId_;
                if ( !current || current == previousSearch ) {
                    return false;
                }
                const auto progress = klogg::ui::apiSession().searchProgress( *current );
                return progress && progress->progress >= 1.0f;
            },
            300000 );
        PHASE_MARKER( "search:done" );
    }
};

namespace {

struct PerfReproFixture {
    std::shared_ptr<Session> appSession = std::make_shared<Session>();
    std::unique_ptr<MainWindow> window;
    CrawlerWidget::access_by<PerfReproAccess> crawler;

    bool openFile( const QString& fileName, LinesCount::UnderlyingType minExpectedLines )
    {
        WindowSession windowSession{ appSession, "PerfRepro", 0 };
        window.reset( new MainWindow( windowSession ) );
        window->resize( 1280, 800 );
        window->show();
        QTest::qWait( 200 );

        // Run the load inside the event loop like the mainwindow harness
        QEventLoop loop;
        QTimer::singleShot( 0, window.get(), [ this, &fileName, &loop ] {
            window->loadInitialFile( fileName, false );
            loop.quit();
        } );
        loop.exec();

        auto* tabs = window->findChild<TabbedCrawlerWidget*>();
        if ( tabs == nullptr || !waitUiState( [ & ] { return tabs->count() == 1; } ) ) {
            LOG_ERROR << "no crawler tab after load";
            return false;
        }

        crawler.crawler = qobject_cast<CrawlerWidget*>( tabs->widget( 0 ) );
        if ( crawler.crawler == nullptr ) {
            LOG_ERROR << "tab widget is not a CrawlerWidget";
            return false;
        }

        return waitUiState(
                   [ & ] { return crawler.mainLines().get() >= minExpectedLines; }, 300000 )
               && waitUiState( [ & ] { return crawler.isLoadingFinished(); }, 300000 );
    }
};

} // namespace

TEST_CASE( "perf repro: resize, search click, tab switch on a big file", "[perf_repro]" )
{
    const auto reproFile = qEnvironmentVariable( "KLOGG_PERF_REPRO_FILE" );
    if ( reproFile.isEmpty() || !QFileInfo::exists( reproFile ) ) {
        QSKIP( "KLOGG_PERF_REPRO_FILE is not set or missing" );
    }

    const auto pattern = qEnvironmentVariableIsSet( "KLOGG_PERF_REPRO_PATTERN" )
        ? qEnvironmentVariable( "KLOGG_PERF_REPRO_PATTERN" )
        : QStringLiteral( "0" );

    PerfReproFixture fixture;
    REQUIRE( fixture.openFile( reproFile, 1000000 ) );
    PHASE_MARKER( "loaded" );

    // Phase 1: drag the main/filtered splitter 20 steps across the widget
    const int dragMin = fixture.crawler.height() / 4;
    const int dragMax = fixture.crawler.height() * 3 / 4;
    for ( int i = 0; i < 20; ++i ) {
        const int pos = dragMin + ( dragMax - dragMin ) * ( i % 10 ) / 9;
        PHASE_MARKER( "resize:step" );
        fixture.crawler.dragSplitter( pos );
        QTest::qWait( 30 );
    }
    PHASE_MARKER( "resize:done" );

    // Phase 2: click Search (filtered view empties, then results flow in)
    fixture.crawler.runSearchAndWait( pattern );
    QTest::qWait( 500 );
    LOG_WARNING << "perf_repro filtered lines: " << fixture.crawler.filteredLines();

    // Drag again with the search bound: this is the reported stutter window
    for ( int i = 0; i < 20; ++i ) {
        const int pos = dragMin + ( dragMax - dragMin ) * ( i % 10 ) / 9;
        PHASE_MARKER( "resize2:step" );
        fixture.crawler.dragSplitter( pos );
        QTest::qWait( 30 );
    }
    PHASE_MARKER( "resize2:done" );

    // Phase 2b (A/B): same search with the overview hidden — isolates the
    // overview full-pull contention from the engine's own search time
    auto& config = Configuration::get();
    const bool wasOverviewVisible = config.isOverviewVisible();
    config.setOverviewVisible( false );
    fixture.crawler.crawler->applyConfiguration();
    QTest::qWait( 200 );

    fixture.crawler.runSearchAndWait( pattern );
    PHASE_MARKER( "search2:done" );

    config.setOverviewVisible( wasOverviewVisible );
    fixture.crawler.crawler->applyConfiguration();
    QTest::qWait( 200 );

    // Phase 3: open a small second tab, switch back and forth
    QTemporaryFile smallFile;
    REQUIRE( smallFile.open() );
    for ( int i = 0; i < 200; ++i ) {
        smallFile.write( QByteArrayLiteral( "small tab line " ) + QByteArray::number( i )
                         + QByteArrayLiteral( "\n" ) );
    }
    smallFile.flush();

    {
        QEventLoop loop;
        QTimer::singleShot( 0, fixture.window.get(), [ & ] {
            fixture.window->loadFileNonInteractive( smallFile.fileName() );
            loop.quit();
        } );
        loop.exec();
    }

    auto* tabs = fixture.window->findChild<TabbedCrawlerWidget*>();
    REQUIRE( waitUiState( [ & ] { return tabs->count() == 2; }, 60000 ) );

    for ( int i = 0; i < 4; ++i ) {
        PHASE_MARKER( "tab:toSmall" );
        tabs->setCurrentIndex( 1 );
        QTest::qWait( 300 );
        PHASE_MARKER( "tab:toBig" );
        tabs->setCurrentIndex( 0 );
        QTest::qWait( 500 );
    }
    PHASE_MARKER( "tab:done" );
}
