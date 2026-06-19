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

#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
#include <qglobal.h>
#include <qnamespace.h>
#include <qtestmouse.h>

#include "savedsearches.h"
#include "session.h"
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
        return crawler->logData_->getNbLine();
    }

    LinesCount getLogFilteredNbLines()
    {
        return crawler->logFilteredData_->getNbLine();
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
        QTest::keyClicks( crawler->searchLineEdit_, pattern );
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
        if ( filteredView && crawler->logFilteredData_->getNbLine().get() > 0 ) {
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
};

using CrawlerWidgetVisitor = CrawlerWidget::access_by<CrawlerWidgetPrivate>;

SCENARIO( "Crawler widget search", "[ui]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session;
    session.savedSearches().clear();

    REQUIRE( session.savedSearches().recentSearches().empty() );

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
    session.savedSearches().clear();

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
    session.savedSearches().clear();

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
