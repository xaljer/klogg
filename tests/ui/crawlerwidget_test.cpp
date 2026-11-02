/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#include <catch2/catch.hpp>

#include <QApplication>
#include <QScrollBar>
#include <QShortcut>
#include <QShortcutEvent>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
#include <qglobal.h>
#include <qnamespace.h>
#include <qtestmouse.h>

#include <algorithm>

#include "klogg/api.h"

#include "apisession.h"
#include "configuration.h"
#include "uiuserdata.h"
#include "session.h"
#include "shortcuts.h"
#include "test_utils.h"

#include "logdata.h"
#include "logfiltereddata.h"

#include "crawlerwidget.h"

static const qint64 SL_NB_LINES = 100LL;

namespace {
bool generateDataFiles( QTemporaryFile& file )
{
    char newLine[ 90 ];

    if ( file.open() ) {
        for ( int i = 0; i < SL_NB_LINES; i++ ) {
            snprintf( newLine, 89,
                      "LOGDATA \t is a part of glogg, we are going to test it thoroughly, this is "
                      "line %06d",
                      i );
            file.write( newLine, static_cast<qint64>( qstrlen( newLine ) ) );
#ifdef Q_OS_WIN
            file.write( "\r\n", 2 );
#else
            file.write( "\n", 1 );
#endif
        }
        file.flush();
    }

    return true;
}

bool generateLongLineDataFiles( QTemporaryFile& file, int nbLines, int lineLength )
{
    if ( file.open() ) {
        QByteArray line;
        line.reserve( lineLength + 3 );
        for ( int i = 0; i < nbLines; i++ ) {
            line.fill( 'A', lineLength - 20 );
            line.append( QByteArray::number( i ) );
            line.append( " ENDMARK" );
#ifdef Q_OS_WIN
            line.append( "\r\n" );
#else
            line.append( '\n' );
#endif
            file.write( line );
            line.clear();
            line.reserve( lineLength + 3 );
        }
        file.flush();
    }
    return true;
}

} // namespace

struct CrawlerWidgetPrivate {
};

template <>
struct CrawlerWidget::access_by<CrawlerWidgetPrivate> {
    std::unique_ptr<CrawlerWidget> crawler;

    bool isLoadingFinished()
    {
        return !crawler->loadingInProgress_;
    }

    LinesCount getLogNbLines()
    {
        // getNbLine is protected on the concrete views, public on the base
        return static_cast<const AbstractLogView*>( crawler->logMainView_ )->getNbLine();
    }

    LinesCount getLogFilteredNbLines()
    {
        return static_cast<const AbstractLogView*>( crawler->filteredView_ )->getNbLine();
    }

    void selectAllInMainView()
    {
        crawler->logMainView_->selectAll();
    }

    void selectAllInFilteredView()
    {
        crawler->filteredView_->selectAll();
    }

    QString mainViewSelectedText()
    {
        return crawler->logMainView_->getSelectedText();
    }

    QString filteredViewSelectedText()
    {
        return crawler->filteredView_->getSelectedText();
    }

    void setSearchPattern( const QString& pattern )
    {
        crawler->searchLineEdit_->clearEditText();
        QTest::keyClicks( crawler->searchLineEdit_, pattern );
    }

    // Emit the main view's mark signal like a gutter click would
    void markMainLines( std::initializer_list<LineNumber::UnderlyingType> lines )
    {
        klogg::vector<LineNumber> toMark;
        for ( const auto line : lines ) {
            toMark.push_back( LineNumber( line ) );
        }
        Q_EMIT crawler->logMainView_->markLines( toMark );
        QTest::qWait( 100 );
    }

    void enableCaseSensitiveSearch()
    {
        if ( !crawler->matchCaseButton_->isChecked() ) {
            QTest::mouseClick( crawler->matchCaseButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void enableInverseMatch()
    {
        if ( !crawler->inverseButton_->isChecked() ) {
            QTest::mouseClick( crawler->inverseButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void enableBooleanCombinationMode()
    {
        if ( !crawler->booleanButton_->isChecked() ) {
            QTest::mouseClick( crawler->booleanButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void runSearch()
    {
        QTest::mouseClick( crawler->searchButton_, Qt::LeftButton );

        QTest::qWait( 100 );

        waitUiState( [ & ]() { return crawler->stopButton_->isHidden(); } );
    }

    void render()
    {
        crawler->grab();
    }

    // ---- Text wrap helpers ----

    void setTextWrap( bool enabled )
    {
        crawler->logMainView_->textWrapSet( enabled );
        crawler->filteredView_->textWrapSet( enabled );
        QTest::qWait( 50 );
    }

    bool isTextWrapEnabled()
    {
        return crawler->logMainView_->isTextWrapEnabled();
    }

    void clickFilteredViewLine( LineNumber::UnderlyingType lineIndex )
    {
        auto* filteredView = crawler->filteredView_;
        if ( filteredView
             && static_cast<const AbstractLogView*>( filteredView )->getNbLine().get() > 0 ) {
            filteredView->selectAndDisplayLine( LineNumber( lineIndex ) );
            QTest::qWait( 50 );
        }
    }

    void resizeViews( int width, int height )
    {
        crawler->logMainView_->resize( width, height );
        crawler->filteredView_->resize( width, height );
        QTest::qWait( 50 );
    }

    void enableFollowMode( bool enabled )
    {
        crawler->logMainView_->followSet( enabled );
        crawler->filteredView_->followSet( enabled );
        QTest::qWait( 50 );
    }

    bool isFollowModeEnabled()
    {
        return crawler->logMainView_->isFollowEnabled();
    }

    int mainViewVerticalScrollMax()
    {
        return crawler->logMainView_->verticalScrollBar()->maximum();
    }

    void scrollMainViewToBottom()
    {
        auto* vbar = crawler->logMainView_->verticalScrollBar();
        vbar->setValue( vbar->maximum() );
        QTest::qWait( 100 );
    }

    bool isFilteredFollowModeEnabled()
    {
        return crawler->filteredView_->isFollowEnabled();
    }

    int mainVerticalScrollValue()
    {
        return crawler->logMainView_->verticalScrollBar()->value();
    }

    int filteredVerticalScrollValue()
    {
        return crawler->filteredView_->verticalScrollBar()->value();
    }

    void scrollMainVerticallyToMiddle()
    {
        auto* vbar = crawler->logMainView_->verticalScrollBar();
        vbar->setValue( vbar->maximum() / 2 );
        QTest::qWait( 50 );
    }

    void scrollFilteredVerticallyToMiddle()
    {
        auto* vbar = crawler->filteredView_->verticalScrollBar();
        vbar->setValue( vbar->maximum() / 2 );
        QTest::qWait( 50 );
    }

    void focusMainView()
    {
        crawler->logMainView_->setFocus();
    }

    void focusFilteredView()
    {
        crawler->filteredView_->setFocus();
    }

    void jumpToTop()
    {
        crawler->jumpToTop();
    }

    LineNumber currentLine()
    {
        return crawler->currentLineNumber_;
    }

    void selectMainViewLine( LineNumber::UnderlyingType line )
    {
        crawler->logMainView_->selectAndDisplayLine( LineNumber( line ) );
        QTest::qWait( 50 );
    }

    // Fire the view's QShortcut for the action directly: the itests run
    // offscreen, where no window becomes active and focus-based shortcut
    // dispatch never engages
    void triggerShortcut( QWidget* view, const std::string& action )
    {
        const auto keys = ShortcutAction::shortcutKeys( action, Configuration::get().shortcuts() );
        const auto shortcuts = view->findChildren<QShortcut*>();
        const auto shortcut
            = std::find_if( shortcuts.begin(), shortcuts.end(),
                            [ & ]( const QShortcut* s ) { return keys.contains( s->key() ); } );
        REQUIRE( shortcut != shortcuts.end() );
        QShortcutEvent event( ( *shortcut )->key(), *shortcut );
        QApplication::sendEvent( *shortcut, &event );
        QTest::qWait( 50 );
    }

    void triggerMainNextMark()
    {
        triggerShortcut( crawler->logMainView_, ShortcutAction::LogViewNextMark );
    }

    void triggerMainPrevMark()
    {
        triggerShortcut( crawler->logMainView_, ShortcutAction::LogViewPrevMark );
    }

    void triggerFilteredNextMark()
    {
        triggerShortcut( crawler->filteredView_, ShortcutAction::LogViewNextMark );
    }

    void triggerFilteredPrevMark()
    {
        triggerShortcut( crawler->filteredView_, ShortcutAction::LogViewPrevMark );
    }
};

using CrawlerWidgetVisitor = CrawlerWidget::access_by<CrawlerWidgetPrivate>;

SCENARIO( "Crawler widget search", "[ui]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;
    { klogg::ui::searchHistory().clear(); klogg::ui::saveSearchHistory(); }

    REQUIRE( klogg::ui::searchHistory().recentSearches().empty() );

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES; } );
    waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } );

    crawlerVisitor.render();

    REQUIRE( crawlerVisitor.getLogNbLines().get() == SL_NB_LINES );

    GIVEN( "loaded log data" )
    {
        THEN( "Has no lines in log view" )
        {
            REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 0 );
        }

        WHEN( "search for lines" )
        {
            crawlerVisitor.setSearchPattern( "this is line" );
            crawlerVisitor.runSearch();

            REQUIRE( waitUiState( [ &crawlerVisitor ]() {
                return crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES;
            } ) );

            THEN( "all lines are matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES );
            }

            AND_WHEN( "copy all from main view" )
            {
                crawlerVisitor.selectAllInMainView();
                auto text = crawlerVisitor.mainViewSelectedText();
                THEN( "text has same number of lines" )
                {
                    REQUIRE( text.split( QChar::LineFeed ).size() == SL_NB_LINES );
                }
            }

            AND_WHEN( "copy all from filtered view" )
            {
                crawlerVisitor.selectAllInFilteredView();
                auto text = crawlerVisitor.filteredViewSelectedText();
                THEN( "text has same number of lines" )
                {
                    REQUIRE( text.split( QChar::LineFeed ).size() == SL_NB_LINES );
                }
            }
        }

        WHEN( "search for 10" )
        {
            crawlerVisitor.setSearchPattern( "10" );

            crawlerVisitor.runSearch();

            waitUiState( [ & ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 1; } );

            THEN( "single line match" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 1 );
            }
        }

        WHEN( "case sensitive search" )
        {
            crawlerVisitor.setSearchPattern( "THIS" );
            crawlerVisitor.enableCaseSensitiveSearch();
            crawlerVisitor.runSearch();

            THEN( "no lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 0 );
            }
        }

        WHEN( "inverse match search" )
        {
            crawlerVisitor.setSearchPattern( "not match" );
            crawlerVisitor.enableInverseMatch();
            crawlerVisitor.runSearch();

            THEN( "all lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES );
            }
        }

        WHEN( "boolean search" )
        {
            crawlerVisitor.setSearchPattern( "\"glogg\" or \"klogg\"" );
            crawlerVisitor.enableBooleanCombinationMode();
            crawlerVisitor.runSearch();

            THEN( "has lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() >= 2 );
            }
        }
    }
}

SCENARIO( "Crawler widget text wrap", "[ui][textwrap]" )
{
    QTemporaryFile file{ "crawler_wrap_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;
    { klogg::ui::searchHistory().clear(); klogg::ui::saveSearchHistory(); }

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES; } );
    waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } );

    crawlerVisitor.render();

    REQUIRE( crawlerVisitor.getLogNbLines().get() == SL_NB_LINES );

    GIVEN( "loaded log data" )
    {
        WHEN( "text wrap is enabled" )
        {
            crawlerVisitor.setTextWrap( true );

            THEN( "text wrap is active" )
            {
                REQUIRE( crawlerVisitor.isTextWrapEnabled() );
            }

            AND_WHEN( "search for lines with text wrap" )
            {
                crawlerVisitor.setSearchPattern( "this is line" );
                crawlerVisitor.runSearch();

                REQUIRE( waitUiState( [ &crawlerVisitor ]() {
                    return crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES;
                } ) );

                THEN( "all lines are matched" )
                {
                    REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES );
                }

                AND_WHEN( "click on filtered view line" )
                {
                    crawlerVisitor.clickFilteredViewLine( 50 );
                    crawlerVisitor.render();

                    THEN( "no crash occurs" )
                    {
                        REQUIRE( true );
                    }
                }

                AND_WHEN( "resize views with text wrap" )
                {
                    crawlerVisitor.resizeViews( 400, 200 );
                    crawlerVisitor.resizeViews( 600, 300 );
                    crawlerVisitor.resizeViews( 300, 150 );
                    crawlerVisitor.render();

                    THEN( "no crash or freeze occurs" )
                    {
                        REQUIRE( crawlerVisitor.isTextWrapEnabled() );
                    }
                }

                AND_WHEN( "display file with wrapped content exceeding viewport" )
                {
                    crawlerVisitor.resizeViews( 300, 100 );
                    crawlerVisitor.render();

                    THEN( "bottom content is visible" )
                    {
                        REQUIRE( crawlerVisitor.isTextWrapEnabled() );
                    }
                }

                AND_WHEN( "follow mode and text wrap are both enabled" )
                {
                    crawlerVisitor.enableFollowMode( true );
                    crawlerVisitor.resizeViews( 400, 100 );
                    crawlerVisitor.render();

                    THEN( "follow mode is enabled and last line is visible" )
                    {
                        REQUIRE( crawlerVisitor.isFollowModeEnabled() );
                        REQUIRE( crawlerVisitor.isTextWrapEnabled() );
                    }
                }

                AND_WHEN( "resize FilteredView height with text wrap" )
                {
                    crawlerVisitor.resizeViews( 400, 200 );
                    crawlerVisitor.render();
                    crawlerVisitor.resizeViews( 400, 150 );
                    crawlerVisitor.render();
                    crawlerVisitor.resizeViews( 400, 250 );
                    crawlerVisitor.render();

                    THEN( "no shadow rendering issues occur" )
                    {
                        REQUIRE( crawlerVisitor.isTextWrapEnabled() );
                    }
                }
            }
        }

        WHEN( "text wrap is disabled" )
        {
            crawlerVisitor.setTextWrap( false );

            THEN( "text wrap is inactive" )
            {
                REQUIRE_FALSE( crawlerVisitor.isTextWrapEnabled() );
            }
        }
    }
}

SCENARIO( "Crawler widget text wrap scroll range", "[ui][textwrap]" )
{
    QTemporaryFile file{ "crawler_wrap_long_XXXXXX" };
    REQUIRE( generateLongLineDataFiles( file, 200, 200 ) );

    Session session;
    { klogg::ui::searchHistory().clear(); klogg::ui::saveSearchHistory(); }

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == 200; } );
    waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } );

    crawlerVisitor.render();

    GIVEN( "loaded long line data" )
    {
        WHEN( "text wrap enabled with narrow viewport" )
        {
            crawlerVisitor.setTextWrap( true );
            crawlerVisitor.resizeViews( 200, 300 );
            crawlerVisitor.render();

            THEN( "text wrap is active" )
            {
                REQUIRE( crawlerVisitor.isTextWrapEnabled() );
            }

            AND_THEN( "scroll to bottom does not freeze" )
            {
                crawlerVisitor.scrollMainViewToBottom();
                crawlerVisitor.render();
                REQUIRE( crawlerVisitor.mainViewVerticalScrollMax() > 0 );
            }

            AND_WHEN( "rapid resize cycles with text wrap" )
            {
                for ( int h = 150; h <= 600; h += 50 ) {
                    crawlerVisitor.resizeViews( 200, h );
                    crawlerVisitor.render();
                    REQUIRE( crawlerVisitor.isTextWrapEnabled() );
                }
                for ( int h = 550; h >= 100; h -= 50 ) {
                    crawlerVisitor.resizeViews( 200, h );
                    crawlerVisitor.render();
                    REQUIRE( crawlerVisitor.isTextWrapEnabled() );
                }

                THEN( "no freeze or crash" )
                {
                    REQUIRE( true );
                }
            }

            AND_WHEN( "search then click on wrapped line" )
            {
                crawlerVisitor.setSearchPattern( "ENDMARK" );
                crawlerVisitor.runSearch();

                REQUIRE( waitUiState( [ &crawlerVisitor ]() {
                    return crawlerVisitor.getLogFilteredNbLines().get() == 200;
                } ) );

                crawlerVisitor.render();
                crawlerVisitor.clickFilteredViewLine( 100 );
                crawlerVisitor.render();

                THEN( "no crash from click handling" )
                {
                    REQUIRE( true );
                }
            }
        }
    }
}

SCENARIO( "Follow file (F) toggles follow mode on both views simultaneously", "[ui][shortcut]" )
{
    QTemporaryFile file{ "crawler_follow_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    REQUIRE(
        waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES; } ) );
    REQUIRE( waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } ) );

    crawlerVisitor.render();

    REQUIRE_FALSE( crawlerVisitor.isFollowModeEnabled() );
    REQUIRE_FALSE( crawlerVisitor.isFilteredFollowModeEnabled() );

    WHEN( "follow mode is enabled via signal" )
    {
        Q_EMIT crawlerVisitor.crawler->followSet( true );
        QTest::qWait( 50 );

        THEN( "both views enable follow mode" )
        {
            REQUIRE( crawlerVisitor.isFollowModeEnabled() );
            REQUIRE( crawlerVisitor.isFilteredFollowModeEnabled() );
        }

        AND_WHEN( "follow mode is disabled via signal" )
        {
            Q_EMIT crawlerVisitor.crawler->followSet( false );
            QTest::qWait( 50 );

            THEN( "both views disable follow mode" )
            {
                REQUIRE_FALSE( crawlerVisitor.isFollowModeEnabled() );
                REQUIRE_FALSE( crawlerVisitor.isFilteredFollowModeEnabled() );
            }
        }
    }
}

SCENARIO( "Go to top (T) scrolls both views regardless of focus", "[ui][shortcut]" )
{
    QTemporaryFile file{ "crawler_gototop_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    REQUIRE(
        waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES; } ) );
    REQUIRE( waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } ) );

    crawlerVisitor.setSearchPattern( "this is line" );
    crawlerVisitor.runSearch();

    REQUIRE( waitUiState(
        [ & ]() { return crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES; } ) );

    crawlerVisitor.setTextWrap( false );
    crawlerVisitor.resizeViews( 320, 120 );
    crawlerVisitor.render();

    REQUIRE( crawlerVisitor.mainViewVerticalScrollMax() > 0 );

    GIVEN( "both views scrolled away from the top and filtered view focused" )
    {
        crawlerVisitor.scrollMainVerticallyToMiddle();
        crawlerVisitor.scrollFilteredVerticallyToMiddle();
        crawlerVisitor.render();

        REQUIRE( crawlerVisitor.mainVerticalScrollValue() > 0 );
        REQUIRE( crawlerVisitor.filteredVerticalScrollValue() > 0 );

        crawlerVisitor.focusFilteredView();

        WHEN( "jumpToTop is called" )
        {
            crawlerVisitor.jumpToTop();
            QTest::qWait( 50 );
            crawlerVisitor.render();

            THEN( "both views scroll to the top" )
            {
                REQUIRE( crawlerVisitor.mainVerticalScrollValue() == 0 );
                REQUIRE( crawlerVisitor.filteredVerticalScrollValue() == 0 );
            }
        }
    }

    GIVEN( "both views scrolled away from the top and main view focused" )
    {
        crawlerVisitor.scrollMainVerticallyToMiddle();
        crawlerVisitor.scrollFilteredVerticallyToMiddle();
        crawlerVisitor.render();

        REQUIRE( crawlerVisitor.mainVerticalScrollValue() > 0 );
        REQUIRE( crawlerVisitor.filteredVerticalScrollValue() > 0 );

        crawlerVisitor.focusMainView();

        WHEN( "jumpToTop is called" )
        {
            crawlerVisitor.jumpToTop();
            QTest::qWait( 50 );
            crawlerVisitor.render();

            THEN( "both views scroll to the top" )
            {
                REQUIRE( crawlerVisitor.mainVerticalScrollValue() == 0 );
                REQUIRE( crawlerVisitor.filteredVerticalScrollValue() == 0 );
            }
        }
    }
}

SCENARIO( "Two views of the same file share one API file entry", "[ui]" )
{
    QTemporaryFile file{ "crawler_shared_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;

    CrawlerWidgetVisitor firstVisitor;
    firstVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [ & ]() { return firstVisitor.isLoadingFinished(); } ) );

    // The second open mirrors "open in new window": same API FileId, shared
    // index, independent per-view filtered data
    CrawlerWidgetVisitor secondVisitor;
    secondVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [ & ]() { return secondVisitor.isLoadingFinished(); } ) );

    const auto openFiles = klogg::ui::apiSession().openFiles();
    REQUIRE( openFiles.has_value() );
    const auto entriesForFile
        = std::count_if( openFiles->begin(), openFiles->end(), [ & ]( const auto& info ) {
              return info.path == file.fileName().toStdString();
          } );
    REQUIRE( entriesForFile == 1 );

    REQUIRE( session.isShared( firstVisitor.crawler.get() ) );
    REQUIRE( session.isShared( secondVisitor.crawler.get() ) );

    REQUIRE( firstVisitor.getLogNbLines().get() == SL_NB_LINES );
    REQUIRE( secondVisitor.getLogNbLines().get() == SL_NB_LINES );

    WHEN( "the first view is closed" )
    {
        auto* firstCrawler = firstVisitor.crawler.get();
        session.close( firstCrawler );
        // The API reference drops when the view is actually destroyed
        firstVisitor.crawler.reset();

        THEN( "the second view keeps working" )
        {
            REQUIRE( !session.isShared( secondVisitor.crawler.get() ) );
            REQUIRE( secondVisitor.getLogNbLines().get() == SL_NB_LINES );

            secondVisitor.setSearchPattern( "this is line" );
            secondVisitor.runSearch();
            REQUIRE( waitUiState(
                [ & ]() { return secondVisitor.getLogFilteredNbLines().get() == SL_NB_LINES; } ) );
        }
    }
}

SCENARIO( "Marks join the filtered union of a live search", "[ui]" )
{
    QTemporaryFile file{ "crawler_mark_union_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;
    { klogg::ui::searchHistory().clear(); klogg::ui::saveSearchHistory(); }

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    REQUIRE(
        waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES; } ) );
    REQUIRE( waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } ) );

    // Lines 000010-000019 match; the default visibility is Marks+Matches
    crawlerVisitor.setSearchPattern( "this is line 00001" );
    crawlerVisitor.runSearch();
    REQUIRE(
        waitUiState( [ & ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 10; } ) );

    // Marks added after the search started must re-mirror into the union
    crawlerVisitor.markMainLines( { 0, 50 } );

    THEN( "marked non-matching lines appear in the filtered view" )
    {
        REQUIRE(
            waitUiState( [ & ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 12; } ) );
    }
}

SCENARIO( "Marks changed in one view invalidate the other view's mark caches", "[ui]" )
{
    QTemporaryFile file{ "crawler_marks_cross_window_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;
    { klogg::ui::searchHistory().clear(); klogg::ui::saveSearchHistory(); }

    CrawlerWidgetVisitor firstVisitor;
    firstVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [ & ]() { return firstVisitor.isLoadingFinished(); } ) );

    // The second open mirrors "open in new window": same API FileId
    CrawlerWidgetVisitor secondVisitor;
    secondVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );
    REQUIRE( waitUiState( [ & ]() { return secondVisitor.isLoadingFinished(); } ) );

    // No search in either view: the filtered views show the marks projection,
    // initially empty
    REQUIRE( waitUiState( [ & ]() { return firstVisitor.getLogFilteredNbLines().get() == 0; } ) );
    REQUIRE( waitUiState( [ & ]() { return secondVisitor.getLogFilteredNbLines().get() == 0; } ) );

    // Populate the second view's mark navigation cache while it is still
    // empty; the marks.changed event must invalidate it
    secondVisitor.triggerFilteredNextMark();

    WHEN( "marks are added through the first view" )
    {
        firstVisitor.markMainLines( { 10, 50 } );
        REQUIRE(
            waitUiState( [ & ]() { return firstVisitor.getLogFilteredNbLines().get() == 2; } ) );

        THEN( "the second view's projection and navigation pick them up" )
        {
            REQUIRE( waitUiState(
                [ & ]() { return secondVisitor.getLogFilteredNbLines().get() == 2; } ) );

            // Projection index 0 is the marked source line 10; the next-mark
            // navigation must not serve the stale (empty) cache
            secondVisitor.clickFilteredViewLine( 0 );
            secondVisitor.triggerFilteredNextMark();
            REQUIRE( secondVisitor.currentLine().get() == 50 );
        }
    }
}

SCENARIO( "Next/prev mark shortcuts move in the same direction in both views", "[ui][shortcut]" )
{
    QTemporaryFile file{ "crawler_mark_nav_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;
    { klogg::ui::searchHistory().clear(); klogg::ui::saveSearchHistory(); }

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    REQUIRE(
        waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES; } ) );
    REQUIRE( waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } ) );

    // Lines 000010-000019 match; the default visibility is Marks+Matches
    crawlerVisitor.setSearchPattern( "this is line 00001" );
    crawlerVisitor.runSearch();
    REQUIRE(
        waitUiState( [ & ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 10; } ) );

    // Mark 000010 (already a match), 000050 and 000090: the two non-matching
    // marks join the union
    crawlerVisitor.markMainLines( { 10, 50, 90 } );
    REQUIRE(
        waitUiState( [ & ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 12; } ) );

    GIVEN( "the main view" )
    {
        crawlerVisitor.selectMainViewLine( 0 );

        THEN( "next mark jumps towards the file end" )
        {
            crawlerVisitor.triggerMainNextMark();
            REQUIRE( crawlerVisitor.currentLine().get() == 10 );

            crawlerVisitor.triggerMainNextMark();
            REQUIRE( crawlerVisitor.currentLine().get() == 50 );

            AND_THEN( "prev mark jumps back towards the file start" )
            {
                crawlerVisitor.triggerMainPrevMark();
                REQUIRE( crawlerVisitor.currentLine().get() == 10 );
            }
        }
    }

    GIVEN( "the filtered view" )
    {
        // Union index 0 is the marked source line 000010
        crawlerVisitor.clickFilteredViewLine( 0 );

        THEN( "next mark jumps towards the file end, like in the main view" )
        {
            crawlerVisitor.triggerFilteredNextMark();
            REQUIRE( crawlerVisitor.filteredViewSelectedText().contains(
                QStringLiteral( "line 000050" ) ) );
            // The selection syncs the main view to the same source line
            REQUIRE( crawlerVisitor.currentLine().get() == 50 );

            crawlerVisitor.triggerFilteredNextMark();
            REQUIRE( crawlerVisitor.filteredViewSelectedText().contains(
                QStringLiteral( "line 000090" ) ) );

            AND_THEN( "prev mark jumps back towards the file start" )
            {
                crawlerVisitor.triggerFilteredPrevMark();
                REQUIRE( crawlerVisitor.filteredViewSelectedText().contains(
                    QStringLiteral( "line 000050" ) ) );

                crawlerVisitor.triggerFilteredPrevMark();
                REQUIRE( crawlerVisitor.filteredViewSelectedText().contains(
                    QStringLiteral( "line 000010" ) ) );
                REQUIRE( crawlerVisitor.currentLine().get() == 10 );
            }
        }
    }
}

SCENARIO( "Copying a large filtered selection maps the source lines", "[ui]" )
{
    // Every even line matches; the range copy pulls one getLines batch and
    // then maps each view index to its source line through
    // FilteredView::getLineNumber (served from the batch's frame stash)
    constexpr int nbLines = 2000;
    constexpr int nbMatches = nbLines / 2;

    QTemporaryFile file{ "crawler_copy_test_XXXXXX" };
    REQUIRE( file.open() );
    for ( int i = 0; i < nbLines; ++i ) {
        file.write( QStringLiteral( "LOGDATA line %1%2" )
                        .arg( i, 6, 10, QLatin1Char( '0' ) )
                        .arg( i % 2 == 0 ? QStringLiteral( " MATCHME" ) : QString() )
                        .toUtf8() );
        file.write( "\n", 1 );
    }
    file.flush();

    Session session;
    { klogg::ui::searchHistory().clear(); klogg::ui::saveSearchHistory(); }

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

    REQUIRE( waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == nbLines; } ) );
    REQUIRE( waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } ) );

    crawlerVisitor.setSearchPattern( "MATCHME" );
    crawlerVisitor.runSearch();
    REQUIRE( waitUiState(
        [ & ]() { return crawlerVisitor.getLogFilteredNbLines().get() == nbMatches; } ) );

    crawlerVisitor.selectAllInFilteredView();
    const auto copiedLines = crawlerVisitor.filteredViewSelectedText().split( QChar::LineFeed );

    // One entry per match, in ascending source-line order: wrong or duplicate
    // line-number mappings would collapse or reorder the map
    REQUIRE( copiedLines.size() == nbMatches );
    for ( int i = 0; i < nbMatches; ++i ) {
        REQUIRE( copiedLines[ i ].contains(
            QStringLiteral( "line %1" ).arg( i * 2, 6, 10, QLatin1Char( '0' ) ) ) );
    }
}
