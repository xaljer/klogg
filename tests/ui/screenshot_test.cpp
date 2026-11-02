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

#include <algorithm>
#include <atomic>
#include <map>
#include <set>

#include <catch2/catch.hpp>

#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryFile>
#include <QTest>
#include <QTextCodec>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QUuid>

#include "test_utils.h"

#include "klogg/api.h"

#include "abstractlogview.h"
#include "apievents.h"
#include "apisession.h"
#include "configuration.h"
#include "uiuserdata.h"
#include "crawlerwidget.h"
#include "eventconfigdialog.h"
#include "highlightercache.h"
#include "highlighteredit.h"
#include "highlightersdialog.h"
#include "highlightersetedit.h"
#include "highlightersmenu.h"
#include "log.h"
#include "log_qt.h"
#include "logparsingrulesdialog.h"
#include "mainwindow.h"
#include "optionsdialog.h"
#include "predefinedfilterscombobox.h"
#include "predefinedfiltersdialog.h"
#include "quickfindwidget.h"
#include "scratchpad.h"
#include "session.h"
#include "tabbedcrawlerwidget.h"
#include "tabbedscratchpad.h"

// Screenshot scenarios drive the real widgets offscreen and save PNG renders
// for manual inspection. They only run when KLOGG_SCREENSHOT_DIR points at
// the output directory; otherwise every scenario skips without failing.

namespace {

constexpr LinesCount::UnderlyingType kMainLogLines = 5000;
constexpr LinesCount::UnderlyingType kEventLogLines = 600;
constexpr LinesCount::UnderlyingType kWrapLogLines = 150;
constexpr LinesCount::UnderlyingType kExpectedErrors = 714; // kMainLogLines with i % 7 == 6
// i % 5 == 4 lines minus the 142 of them already claimed by the ERROR branch
// (i % 35 == 34): the generator's else-if chain gives ERROR precedence
constexpr LinesCount::UnderlyingType kExpectedWarns = 858;

// Index of the "Events" entry of the filtered view visibility combobox
constexpr int kEventsVisibilityIndex = 6;

struct ScreenshotAccess;

} // namespace

template <>
struct CrawlerWidget::access_by<ScreenshotAccess> {
    // Non-owning: the crawler is owned by the MainWindow tab widget
    CrawlerWidget* crawler = nullptr;

    bool isLoadingFinished() const
    {
        return !crawler->loadingInProgress_;
    }

    // getNbLine is protected on the concrete views, public on the base
    LinesCount mainLines() const
    {
        return static_cast<const AbstractLogView*>( crawler->logMainView_ )->getNbLine();
    }

    LinesCount filteredLines() const
    {
        return static_cast<const AbstractLogView*>( crawler->filteredView_ )->getNbLine();
    }

    QWidget* mainView() const
    {
        return crawler->logMainView_;
    }

    // The view itself, for the paint path switches of the dual path test
    AbstractLogView* mainViewWidget() const
    {
        return crawler->logMainView_;
    }

    QWidget* filteredView() const
    {
        return crawler->filteredView_;
    }

    void runSearch( const QString& pattern )
    {
        crawler->searchLineEdit_->clearEditText();
        QTest::keyClicks( crawler->searchLineEdit_, pattern );
        QTest::mouseClick( crawler->searchButton_, Qt::LeftButton );
        QTest::qWait( 100 );
    }

    // Real gutter click: marks the line of the given visible row
    void clickGutterRow( int row )
    {
        const auto charHeight = crawler->logMainView_->fontMetrics().height();
        const QPoint pos{ 2, row * charHeight + charHeight / 2 };
        QTest::mouseClick( crawler->logMainView_->viewport(), Qt::LeftButton, Qt::NoModifier, pos );
        QTest::qWait( 100 );
    }

    // Scroll to the line, then select it with a real click in the text area
    void clickMainViewLine( LineNumber::UnderlyingType line )
    {
        crawler->logMainView_->selectAndDisplayLine( LineNumber( line ) );
        QTest::qWait( 100 );
        const auto row = static_cast<int>( line - crawler->getTopLine().get() );
        const auto charHeight = crawler->logMainView_->fontMetrics().height();
        const QPoint pos{ crawler->logMainView_->viewport()->width() / 2,
                          row * charHeight + charHeight / 2 };
        QTest::mouseClick( crawler->logMainView_->viewport(), Qt::LeftButton, Qt::NoModifier, pos );
        QTest::qWait( 100 );
    }

    void showEvents()
    {
        crawler->visibilityBox_->setCurrentIndex( kEventsVisibilityIndex );
        QTest::qWait( 100 );
    }

    bool eventsPopulated() const
    {
        return crawler->populatedExtractionId_.has_value() && !crawler->sliceRanges_.isEmpty();
    }

    void setTextWrap( bool enabled )
    {
        crawler->logMainView_->textWrapSet( enabled );
        crawler->filteredView_->textWrapSet( enabled );
        QTest::qWait( 100 );
    }

    // Click a checkable search-mode button into the requested state
    void setMatchCase( bool on )
    {
        setButtonState( crawler->matchCaseButton_, on );
    }

    void setUseRegex( bool on )
    {
        setButtonState( crawler->useRegexpButton_, on );
    }

    void setInverseMatch( bool on )
    {
        setButtonState( crawler->inverseButton_, on );
    }

    void setBooleanMode( bool on )
    {
        setButtonState( crawler->booleanButton_, on );
    }

    // Type the pattern, run the search, then wait for the fresh SearchId to
    // finish on the API side (deterministic even for zero-match searches)
    void runSearchAndWait( const QString& pattern )
    {
        const auto previousSearch = crawler->currentSearchId_;
        crawler->searchLineEdit_->clearEditText();
        QTest::keyClicks( crawler->searchLineEdit_, pattern );
        QTest::mouseClick( crawler->searchButton_, Qt::LeftButton );

        waitUiState( [ & ] {
            const auto& current = crawler->currentSearchId_;
            if ( !current || current == previousSearch ) {
                return false;
            }
            const auto progress = klogg::ui::apiSession().searchProgress( *current );
            return progress && progress->progress >= 1.0f;
        } );
    }

    void keepResultsAndSearch( const QString& pattern )
    {
        if ( !crawler->keepSearchResultsButton_->isChecked() ) {
            QTest::mouseClick( crawler->keepSearchResultsButton_, Qt::LeftButton );
            QTest::qWait( 50 );
        }
        runSearchAndWait( pattern );
    }

    int filteredTabCount() const
    {
        return crawler->tabbedFilteredView_->count();
    }

    void setFilteredTab( int index )
    {
        crawler->tabbedFilteredView_->setCurrentIndex( index );
        QTest::qWait( 100 );
    }

    void clearSearchTextAndRun()
    {
        QTest::mouseClick( crawler->clearButton_, Qt::LeftButton );
        QTest::qWait( 50 );
        QTest::mouseClick( crawler->searchButton_, Qt::LeftButton );
        QTest::qWait( 100 );
    }

    LineNumber currentLine() const
    {
        return crawler->currentLineNumber_;
    }

    QWidget* overview() const
    {
        return crawler->overviewWidget_;
    }

    // The same signal the view's context menu "Set search start/end" feeds
    void setSearchLimitsFromUi( LineNumber::UnderlyingType startLine,
                                LineNumber::UnderlyingType endLine )
    {
        Q_EMIT crawler->logMainView_->changeSearchLimits( LineNumber( startLine ),
                                                          LineNumber( endLine ) );
        QTest::qWait( 100 );
    }

    void clearSearchLimitsFromUi()
    {
        Q_EMIT crawler->logMainView_->clearSearchLimits();
        QTest::qWait( 100 );
    }

    // Real click on the text area of the given visible row (optionally with
    // modifiers, e.g. Shift for range selection)
    void clickMainViewRow( int row, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
    {
        const auto charHeight = crawler->logMainView_->fontMetrics().height();
        const QPoint pos{ crawler->logMainView_->viewport()->width() / 2,
                          row * charHeight + charHeight / 2 };
        QTest::mouseClick( crawler->logMainView_->viewport(), Qt::LeftButton, modifiers, pos );
        QTest::qWait( 100 );
    }

    QString selectionText() const
    {
        return crawler->getSelectedText();
    }

    QWidget* mainViewViewport() const
    {
        return crawler->logMainView_->viewport();
    }

    QComboBox* searchCombo() const
    {
        return crawler->searchLineEdit_;
    }

    PredefinedFiltersComboBox* predefinedFiltersCombo() const
    {
        return crawler->predefinedFilters_;
    }

    void focusMainView()
    {
        crawler->logMainView_->setFocus();
        QTest::qWait( 50 );
    }

    // Viewport position of the given character cell, mirroring the
    // leftMarginPx_ computation in drawTextArea
    QPoint mainViewCellPos( LineNumber::UnderlyingType line, int column ) const
    {
        auto* view = crawler->logMainView_;
        const auto metrics = view->fontMetrics();
        const auto charHeight = metrics.height();
        const auto charWidth = metrics.horizontalAdvance( QLatin1Char( 'm' ) );
        const int digits = static_cast<int>(
            std::to_string( static_cast<const AbstractLogView*>( view )->getNbLine().get() )
                .size() );
        const int gutterPx
            = Configuration::get().mainLineNumbersVisible() ? 2 * 3 + digits * charWidth : 0;
        const int leftMarginPx = 13 + gutterPx;
        const auto row = static_cast<int>( line - crawler->getTopLine().get() );
        const auto lineText = QString::fromStdString(
            static_cast<const AbstractLogView*>( view )->getExpandedLineString(
                LineNumber( line ) ) );
        const int x
            = leftMarginPx + metrics.horizontalAdvance( lineText.left( column ) ) + charWidth / 2;
        return QPoint( x, row * charHeight + charHeight / 2 );
    }

    // Double-click the given character cell: the view's word selection kicks
    // in and selects the whole word as a portion
    void doubleClickMainViewCell( LineNumber::UnderlyingType line, int column )
    {
        QTest::mouseDClick( crawler->logMainView_->viewport(), Qt::LeftButton, Qt::NoModifier,
                            mainViewCellPos( line, column ) );
        QTest::qWait( 100 );
    }

    // Right-click the given character cell (e.g. inside a portion selection)
    void rightClickMainViewCell( LineNumber::UnderlyingType line, int column )
    {
        QTest::mouseClick( crawler->logMainView_->viewport(), Qt::RightButton, Qt::NoModifier,
                           mainViewCellPos( line, column ) );
        QTest::qWait( 100 );
    }

    bool colorLabelsContain( const QString& text ) const
    {
        const auto labels = crawler->colorLabelsManager_.colorLabels();
        return std::any_of( labels.begin(), labels.end(),
                            [ & ]( const auto& words ) { return words.contains( text ); } );
    }

    QWidget* filteredViewViewport() const
    {
        return crawler->filteredView_->viewport();
    }

    bool isFollowEnabled() const
    {
        return crawler->isFollowEnabled();
    }

    QString searchInfoText() const
    {
        return crawler->searchInfoLine_->text();
    }

    QString mainViewLineText( LineNumber::UnderlyingType line ) const
    {
        return QString::fromStdString( static_cast<const AbstractLogView*>( crawler->logMainView_ )
                                           ->getLineString( LineNumber( line ) ) );
    }

    // Click Search without retyping; waits for the fresh SearchId like runSearchAndWait
    void clickSearchAndWait()
    {
        const auto previousSearch = crawler->currentSearchId_;
        QTest::mouseClick( crawler->searchButton_, Qt::LeftButton );

        waitUiState( [ & ] {
            const auto& current = crawler->currentSearchId_;
            if ( !current || current == previousSearch ) {
                return false;
            }
            const auto progress = klogg::ui::apiSession().searchProgress( *current );
            return progress && progress->progress >= 1.0f;
        } );
    }

private:
    static void setButtonState( QToolButton* button, bool on )
    {
        if ( button->isChecked() != on ) {
            QTest::mouseClick( button, Qt::LeftButton );
            QTest::qWait( 50 );
        }
    }
};

namespace {

struct MainWindowFixture {
    std::shared_ptr<Session> appSession = std::make_shared<Session>();
    std::unique_ptr<MainWindow> window;
    CrawlerWidget::access_by<ScreenshotAccess> crawler;

    bool openFile( const QString& fileName, LinesCount::UnderlyingType expectedLines )
    {
        WindowSession windowSession{ appSession, "Screenshot", 0 };
        window.reset( new MainWindow( windowSession ) );
        window->resize( 1280, 800 );

        SafeQSignalSpy activateSpy( window.get(), SIGNAL( windowActivated() ) );
        window->show();
        QTest::qWait( 100 );
        if ( !activateSpy.safeWait() ) {
            LOG_ERROR << "main window was not activated";
            return false;
        }

        // Mirror the mainwindow_test harness: run the load inside the event loop
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

        return waitUiState( [ & ] { return crawler.mainLines().get() == expectedLines; } )
               && waitUiState( [ & ] { return crawler.isLoadingFinished(); } );
    }
};

bool saveShot( QWidget* widget, const QString& dir, const char* name )
{
    QTest::qWait( 200 );
    const QString path = QDir{ dir }.filePath( QString::fromLatin1( name ) );
    if ( !widget->grab().save( path ) ) {
        LOG_ERROR << "failed to save screenshot " << path;
        return false;
    }
    const QFileInfo info{ path };
    LOG_INFO << "screenshot saved: " << path << " size=" << info.size();
    return info.exists() && info.size() > 0;
}

void writeLines( QTemporaryFile& file, const QStringList& lines )
{
    REQUIRE( file.open() );
    QTextStream stream( &file );
    for ( const auto& line : lines ) {
        stream << line <<
#ifdef Q_OS_WIN
            "\r\n";
#else
            "\n";
#endif
    }
    stream.flush();
    file.flush();
}

QStringList mainLogLines()
{
    QStringList lines;
    lines.reserve( kMainLogLines );
    const QDateTime base{ QDate{ 2024, 1, 15 }, QTime{ 10, 0, 0 } };
    for ( qint64 i = 0; i < static_cast<qint64>( kMainLogLines ); ++i ) {
        const auto timestamp = base.addMSecs( i * 37 ).toString( "yyyy-MM-dd HH:mm:ss.zzz" );
        QString level;
        QString message;
        if ( i % 7 == 6 ) {
            level = QStringLiteral( "ERROR" );
            message = QStringLiteral( "Database connection failed, retry scheduled" );
        }
        else if ( i % 5 == 4 ) {
            level = QStringLiteral( "WARN" );
            message = QStringLiteral( "Cache miss for key, falling back to database" );
        }
        else if ( i % 3 == 2 ) {
            level = QStringLiteral( "DEBUG" );
            message = QStringLiteral( "GC pause completed" );
        }
        else {
            level = QStringLiteral( "INFO" );
            message = QStringLiteral( "Request processed successfully" );
        }
        lines.append( QStringLiteral( "%1 [%2] [thread-%3] %4 requestId=%5" )
                          .arg( timestamp, level )
                          .arg( i % 16, 2, 10, QLatin1Char( '0' ) )
                          .arg( message )
                          .arg( i ) );
    }
    return lines;
}

QStringList eventLogLines()
{
    // Slice pairs: begin at these 0-based lines, end 15 lines later
    const std::map<qint64, int> sliceBegins{ { 100, 1 }, { 300, 2 }, { 500, 3 } };
    std::map<qint64, int> sliceEnds;
    for ( const auto& [ begin, tx ] : sliceBegins ) {
        sliceEnds.emplace( begin + 15, tx );
    }

    QStringList lines;
    lines.reserve( kEventLogLines );
    const QDateTime base{ QDate{ 2024, 1, 15 }, QTime{ 10, 0, 0 } };
    int requestId = 0;
    for ( qint64 i = 0; i < static_cast<qint64>( kEventLogLines ); ++i ) {
        const auto timestamp = base.addMSecs( i * 50 ).toString( "yyyy-MM-dd HH:mm:ss.zzz" );
        QString message;
        const auto beginIt = sliceBegins.find( i );
        const auto endIt = sliceEnds.find( i );
        if ( beginIt != sliceBegins.end() ) {
            message = QStringLiteral( "Transaction tx=%1 begin" ).arg( beginIt->second );
        }
        else if ( endIt != sliceEnds.end() ) {
            message = QStringLiteral( "Transaction tx=%1 end status=ok" ).arg( endIt->second );
        }
        else if ( i % 40 == 10 ) {
            message = QStringLiteral( "Request started id=%1" ).arg( ++requestId );
        }
        else if ( i % 20 == 5 ) {
            message = QStringLiteral( "Task finished took=%1ms" ).arg( 20 + i % 50 );
        }
        else {
            message = QStringLiteral( "Routine heartbeat message %1" ).arg( i );
        }
        lines.append( QStringLiteral( "%1 INFO %2" ).arg( timestamp, message ) );
    }
    return lines;
}

QStringList wrapLogLines()
{
    QStringList lines;
    lines.reserve( kWrapLogLines );
    for ( qint64 i = 0; i < static_cast<qint64>( kWrapLogLines ); ++i ) {
        QString line( 380, QLatin1Char( 'A' ) );
        line += QStringLiteral( " wrapline %1 ENDMARK" ).arg( i );
        lines.append( line );
    }
    return lines;
}

void writeUtf16Log( QTemporaryFile& file )
{
    QStringList lines;
    for ( int i = 0; i < 120; ++i ) {
        lines.append( QStringLiteral( "2024-01-15 10:00:00.%1 [INFO] 中文日志内容 "
                                      "encoding screenshot sample line %2" )
                          .arg( i % 1000, 3, 10, QLatin1Char( '0' ) )
                          .arg( i ) );
    }
    // QTextCodec works on both Qt5 and Qt6 (qt5compat); write the BOM by
    // hand as fromUnicode does not emit one
    const auto* codec = QTextCodec::codecForName( "UTF-16LE" );
    REQUIRE( codec );
    REQUIRE( file.open() );
    file.write( "\xFF\xFE", 2 );
    file.write( codec->fromUnicode( lines.join( QLatin1Char( '\n' ) ) + QLatin1Char( '\n' ) ) );
    file.flush();
}

Highlighter screenshotRule( const char* name, const char* pattern, const char* color,
                            bool onlyMatch = false )
{
    Highlighter rule;
    rule.setName( name );
    rule.setPattern( pattern );
    rule.setForeColor( *klogg::Rgba::fromHex( color ) );
    rule.setHighlightOnlyMatch( onlyMatch );
    return rule;
}

// The UI collection is the authority; replacing it and refreshing pushes the
// merged effective rules to the core session
void installScreenshotCollection( HighlighterSetCollection collection )
{
    // Keep the color label palette usable for the scenarios that pick one
    if ( collection.quickHighlighters().empty() ) {
        collection.setQuickHighlighters( defaultQuickHighlighters() );
    }
    auto& cache = klogg::ui::HighlighterCache::instance();
    cache.collection() = std::move( collection );
    cache.refresh();
}

void installScreenshotHighlighters()
{
    HighlighterSetCollection collection;
    auto set = HighlighterSet::createNewSet( "screenshot" );
    set.setHighlighters( { screenshotRule( "errors", "ERROR", "#ffc62828" ),
                           screenshotRule( "warnings", "WARN", "#ff9a6a00" ),
                           screenshotRule( "info", "INFO", "#ff1f6feb" ) } );
    collection.setHighlighterSets( { set } );
    collection.activateSet( set.id() );
    installScreenshotCollection( std::move( collection ) );
}

klogg::api::LogTypeConfig makeEventLogType()
{
    // Fully qualified names: the native UI types in eventvisualization.h
    // share the same names
    klogg::api::LogTypeConfig config{};
    config.name = "Screenshot Event Log";
    config.globPattern = "*.shotlog";
    config.parsingRule.name = "screenshot-parser";
    config.parsingRule.globPattern = "*.shotlog";
    config.parsingRule.parsePattern
        = R"(^(?<timestamp>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) (?<log_level>\w+))";
    config.parsingRule.timestampFormat = "yyyy-MM-dd HH:mm:ss.zzz";

    klogg::api::EventRule request;
    request.name = "request";
    request.category = "http";
    request.eventType = klogg::api::EventType::Instance;
    request.pattern = R"(Request started id=(?<reqid>\d+))";
    request.parameterNames = { "reqid" };

    klogg::api::EventRule transaction;
    transaction.name = "transaction";
    transaction.category = "db";
    transaction.eventType = klogg::api::EventType::Slice;
    transaction.beginPattern = R"(Transaction tx=(?<tx>\d+) begin)";
    transaction.endPattern = R"(Transaction tx=(?<tx>\d+) end status=(?<status>\w+))";
    transaction.asyncIdParam = "tx";
    transaction.parameterNames = { "tx" };
    transaction.endParameterNames = { "status" };

    klogg::api::EventRule task;
    task.name = "task";
    task.category = "compute";
    task.eventType = klogg::api::EventType::Slice;
    task.beginPattern = R"(Task finished took=(?<dur>\d+)ms)";
    task.durationParam = "dur";
    task.durationUnit = klogg::api::DurationUnit::Ms;
    task.parameterNames = { "dur" };

    config.eventRulesByCategory[ "http" ] = { request };
    config.eventRulesByCategory[ "db" ] = { transaction };
    config.eventRulesByCategory[ "compute" ] = { task };
    return config;
}

void removeStaleEventConfigs()
{
    auto configs = klogg::ui::listLogTypeConfigs();
    configs.erase( std::remove_if( configs.begin(), configs.end(),
                                   []( const auto& config ) {
                                       return config.name == "Screenshot Event Log";
                                   } ),
                   configs.end() );
    REQUIRE( klogg::ui::replaceLogTypeConfigs( std::move( configs ) ) );
}

// Arms a callback for the next modal dialog of type DialogT; the caller arms it
// first and then triggers the action whose exec() blocks until the callback
// closes the dialog. Retries while the dialog is coming up.
template <typename DialogT, typename Fn>
void whenModalDialog( Fn&& fn, int attemptsLeft = 50 )
{
    QTimer::singleShot( 100, qApp, [ fn = std::forward<Fn>( fn ), attemptsLeft ]() mutable {
        if ( auto* dialog = qobject_cast<DialogT*>( QApplication::activeModalWidget() ) ) {
            fn( dialog );
            return;
        }
        if ( attemptsLeft > 0 ) {
            whenModalDialog<DialogT>( std::move( fn ), attemptsLeft - 1 );
        }
        else {
            LOG_ERROR << "modal dialog did not appear in time";
            if ( auto* modal = QApplication::activeModalWidget() ) {
                modal->close();
            }
        }
    } );
}

// Context menus exec as popups, not modal widgets
template <typename Fn>
void whenPopupMenu( Fn&& fn, int attemptsLeft = 50 )
{
    QTimer::singleShot( 100, qApp, [ fn = std::forward<Fn>( fn ), attemptsLeft ]() mutable {
        if ( auto* menu = qobject_cast<QMenu*>( QApplication::activePopupWidget() ) ) {
            fn( menu );
            return;
        }
        if ( attemptsLeft > 0 ) {
            whenPopupMenu( std::move( fn ), attemptsLeft - 1 );
        }
        else {
            LOG_ERROR << "popup menu did not appear in time";
        }
    } );
}

// Polling wait usable inside a modal loop (some dialog setup is posted)
template <typename Fn>
bool waitInModal( Fn&& condition )
{
    for ( int i = 0; i < 100; ++i ) {
        if ( condition() ) {
            return true;
        }
        QTest::qWait( 50 );
    }
    return false;
}

QAction* findActionByText( const QList<QAction*>& actions, const QString& textPart )
{
    for ( auto* action : actions ) {
        if ( action->text().contains( textPart, Qt::CaseInsensitive ) ) {
            return action;
        }
    }
    return nullptr;
}

HighlightersMenu* findHighlightersMenu( MainWindow* window )
{
    for ( auto* action : window->menuBar()->actions() ) {
        if ( auto* menu = qobject_cast<HighlightersMenu*>( action->menu() ) ) {
            return menu;
        }
    }
    return nullptr;
}

QMenu* findTopMenu( MainWindow* window, const QString& titlePart )
{
    for ( auto* action : window->menuBar()->actions() ) {
        if ( action->menu() != nullptr
             && action->text().contains( titlePart, Qt::CaseInsensitive ) ) {
            return action->menu();
        }
    }
    return nullptr;
}

TabbedScratchPad* findScratchPadWindow()
{
    for ( auto* widget : QApplication::topLevelWidgets() ) {
        if ( auto* scratchPad = qobject_cast<TabbedScratchPad*>( widget ) ) {
            return scratchPad;
        }
    }
    return nullptr;
}

bool activeHighlighterCacheHasPattern( const char* pattern )
{
    const auto& rules = klogg::ui::HighlighterCache::instance().activeSet().highlighters();
    return std::any_of( rules.begin(), rules.end(), [ & ]( const Highlighter& rule ) {
        return rule.pattern() == std::string( pattern );
    } );
}

// ---- File-behavior helpers ----

// Append lines through a short-lived handle, like an external logger would;
// writes through the long-lived QTemporaryFile handle do not reliably surface
// to the directory watcher (Windows publishes the change on handle close)
void appendLines( const QString& filePath, const QStringList& lines )
{
    QFile file{ filePath };
    if ( !file.open( QIODevice::Append | QIODevice::Text ) ) {
        LOG_ERROR << "failed to open for append: " << filePath;
        return;
    }
    QTextStream stream( &file );
    for ( const auto& line : lines ) {
        stream << line <<
#ifdef Q_OS_WIN
            "\r\n";
#else
            "\n";
#endif
    }
    stream.flush();
    file.flush();
}

// Truncate the file and rewrite it with the given lines
void rewriteLines( const QString& filePath, const QStringList& lines )
{
    QFile file{ filePath };
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) ) {
        LOG_ERROR << "failed to open for rewrite: " << filePath;
        return;
    }
    file.close();
    appendLines( filePath, lines );
}

// Drop every log type config, not just the event sample one
void removeAllLogTypeConfigs()
{
    REQUIRE( klogg::ui::replaceLogTypeConfigs( {} ) );
}

template <typename T>
T* findTopLevelWidget()
{
    for ( auto* widget : QApplication::topLevelWidgets() ) {
        if ( auto* typed = qobject_cast<T*>( widget ) ) {
            return typed;
        }
    }
    return nullptr;
}

QDialog* findTopLevelDialogByTitle( const QString& title )
{
    for ( auto* widget : QApplication::topLevelWidgets() ) {
        auto* dialog = qobject_cast<QDialog*>( widget );
        if ( dialog != nullptr && dialog->windowTitle() == title ) {
            return dialog;
        }
    }
    return nullptr;
}


// Fill in and accept the non-native save dialog that export/save flows open
void armSaveDialogAnswer( const QString& directory, const QString& fileName )
{
    whenModalDialog<QFileDialog>( [ directory, fileName ]( QFileDialog* dialog ) {
        dialog->setDirectory( directory );
        auto* nameEdit = dialog->findChild<QLineEdit*>( QStringLiteral( "fileNameEdit" ) );
        if ( nameEdit != nullptr ) {
            nameEdit->setText( fileName );
        }
        // accept()/done() are protected on QFileDialog; click its Save button
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if ( buttons != nullptr ) {
            for ( const auto role : { QDialogButtonBox::Save, QDialogButtonBox::Ok } ) {
                if ( auto* button = buttons->button( role ) ) {
                    QTest::mouseClick( button, Qt::LeftButton );
                    break;
                }
            }
        }
    } );
}

} // namespace

SCENARIO( "Screenshots: open file, search and marks", "[screenshot]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    installScreenshotHighlighters();

    QTemporaryFile file{ QDir::temp().filePath( "klogg_shot_main_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "01_mainwindow_indexed.png" ) );

    fixture.crawler.runSearch( QStringLiteral( "ERROR" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "02_search_filtered.png" ) );

    // Rows 2, 5 and 9 never match the ERROR search, so they land in the
    // filtered projection only through the Marks visibility
    fixture.crawler.clickGutterRow( 2 );
    fixture.crawler.clickGutterRow( 5 );
    fixture.crawler.clickGutterRow( 9 );

    const auto fileId = fixture.crawler.crawler->fileId();
    REQUIRE( waitUiState( [ & ] {
        const auto marks = klogg::ui::apiSession().listMarks( fileId );
        return marks && marks->size() == 3;
    } ) );
    REQUIRE( waitUiState(
        [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors + 3; } ) );

    REQUIRE( saveShot( fixture.crawler.mainView(), screenshotDir, "03_marks_mainview.png" ) );
    REQUIRE(
        saveShot( fixture.crawler.filteredView(), screenshotDir, "04_marks_filteredview.png" ) );
}

SCENARIO( "Screenshots: event extraction and slice overlay", "[screenshot]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    removeStaleEventConfigs();
    REQUIRE( klogg::ui::replaceLogTypeConfigs( { makeEventLogType() } ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_shot_events_XXXXXX.shotlog" ) };
    writeLines( file, eventLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kEventLogLines ) );

    fixture.crawler.showEvents();
    // Extraction and the populate round-trip cross the worker and thread-pool
    // hops; give them a wider budget than the usual UI settle
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.eventsPopulated(); }, 30000 ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "05_events_view.png" ) );

    // Line 108 sits inside the first slice (tx=1 spans lines 100-115)
    fixture.crawler.clickMainViewLine( 108 );

    auto* visualizeAction = fixture.window->visualizeInKloggActionForTest();
    REQUIRE( visualizeAction != nullptr );
    visualizeAction->setChecked( true );
    QTest::qWait( 100 );

    REQUIRE( waitUiState( [ & ] { return fixture.window->isSliceOverlayVisibleForTest(); } ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "06_slice_overlay.png" ) );

    removeStaleEventConfigs();
}

SCENARIO( "Screenshots: text wrap and encoding switch", "[screenshot]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    {
        QTemporaryFile file{ QDir::temp().filePath( "klogg_shot_wrap_XXXXXX.log" ) };
        writeLines( file, wrapLogLines() );

        MainWindowFixture fixture;
        REQUIRE( fixture.openFile( file.fileName(), kWrapLogLines ) );

        fixture.crawler.setTextWrap( false );
        REQUIRE( saveShot( fixture.window.get(), screenshotDir, "07_textwrap_off.png" ) );

        fixture.crawler.setTextWrap( true );
        REQUIRE( fixture.crawler.crawler->isTextWrapEnabled() );
        REQUIRE( saveShot( fixture.window.get(), screenshotDir, "08_textwrap_on.png" ) );
    }

    {
        QTemporaryFile file{ QDir::temp().filePath( "klogg_shot_utf16_XXXXXX.log" ) };
        writeUtf16Log( file );

        MainWindowFixture fixture;
        REQUIRE( fixture.openFile( file.fileName(), 120 ) );

        REQUIRE( waitUiState(
            [ & ] { return fixture.crawler.crawler->encodingText().contains( "UTF-16" ); } ) );
        REQUIRE(
            saveShot( fixture.window.get(), screenshotDir, "09_encoding_utf16_detected.png" ) );

        // The forced codec change reindexes; completion fires "file.reindexed"
        std::atomic<bool> reindexed{ false };
        const auto fileId = fixture.crawler.crawler->fileId();
        klogg::ui::apiSession().subscribe( [ fileId, &reindexed ]( const std::string& type,
                                                                   const std::string& json ) {
            if ( type == "file.reindexed"
                 && json.find( "\"fileId\":" + std::to_string( fileId ) ) != std::string::npos ) {
                reindexed = true;
            }
        } );

        // ISO-8859-1 mib: UTF-16 content rendered through a single-byte codec
        fixture.crawler.crawler->setEncoding( 4 );
        REQUIRE( waitUiState( [ & ] { return reindexed.load(); } ) );
        REQUIRE( waitUiState(
            [ & ] { return fixture.crawler.crawler->encodingText().contains( "ISO-8859-1" ); } ) );
        REQUIRE( saveShot( fixture.window.get(), screenshotDir, "10_encoding_forced_latin1.png" ) );
    }
}

// ---- Search and navigation coverage ----

SCENARIO( "Screenshots: search modes (case, regex, inverse, boolean)", "[screenshot][search]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    installScreenshotHighlighters();

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx1_modes_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    // "database" case-insensitively hits both ERROR ("Database ...") and WARN
    // ("... database") lines; case-sensitive splits the two
    fixture.crawler.runSearchAndWait( QStringLiteral( "database" ) );
    REQUIRE( waitUiState( [ & ] {
        return fixture.crawler.filteredLines().get() == kExpectedErrors + kExpectedWarns;
    } ) );

    fixture.crawler.setMatchCase( true );
    fixture.crawler.runSearchAndWait( QStringLiteral( "database" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedWarns; } ) );

    fixture.crawler.runSearchAndWait( QStringLiteral( "Database" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "11_case_sensitive.png" ) );

    // klogg defaults to regex mode (mainRegexpType = ExtendedRegexp); with
    // the mode explicitly off the metacharacters match nothing literally
    fixture.crawler.setMatchCase( false );
    fixture.crawler.setUseRegex( false );
    fixture.crawler.runSearchAndWait( QStringLiteral( "ERROR.*retry" ) );
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == 0; } ) );

    fixture.crawler.setUseRegex( true );
    fixture.crawler.runSearchAndWait( QStringLiteral( "ERROR.*retry" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "12_regex_search.png" ) );

    // Inverse match: the complement of the ERROR set
    fixture.crawler.setUseRegex( false );
    fixture.crawler.setInverseMatch( true );
    fixture.crawler.runSearchAndWait( QStringLiteral( "ERROR" ) );
    REQUIRE( waitUiState( [ & ] {
        return fixture.crawler.filteredLines().get() == kMainLogLines - kExpectedErrors;
    } ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "13_inverse_match.png" ) );

    // Boolean AND of two terms
    fixture.crawler.setInverseMatch( false );
    fixture.crawler.setBooleanMode( true );
    fixture.crawler.runSearchAndWait( QStringLiteral( "\"WARN\" and \"Cache\"" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedWarns; } ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "14_boolean_and.png" ) );
}

SCENARIO( "Screenshots: keep results forks a filtered tab", "[screenshot][search]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx1_keep_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    fixture.crawler.runSearchAndWait( QStringLiteral( "ERROR" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );

    fixture.crawler.keepResultsAndSearch( QStringLiteral( "WARN" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedWarns; } ) );

    REQUIRE( fixture.crawler.filteredTabCount() == 2 );

    // The kept tab still renders the interrupted ERROR result set
    fixture.crawler.setFilteredTab( 0 );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );

    fixture.crawler.setFilteredTab( 1 );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedWarns; } ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "15_keep_results.png" ) );
}

SCENARIO( "Screenshots: quickfind incremental navigation", "[screenshot][search]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx1_qf_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    QTest::keyClick( fixture.window.get(), Qt::Key_F, Qt::ControlModifier );
    auto* qfWidget = fixture.window->findChild<QuickFindWidget*>();
    REQUIRE( qfWidget != nullptr );
    REQUIRE( waitUiState( [ & ] { return qfWidget->isVisible(); } ) );

    auto* qfEdit = qfWidget->findChild<QLineEdit*>();
    REQUIRE( qfEdit != nullptr );

    QToolButton* nextButton = nullptr;
    QToolButton* previousButton = nullptr;
    for ( auto* button : qfWidget->findChildren<QToolButton*>() ) {
        if ( button->text() == QLatin1String( "Next" ) ) {
            nextButton = button;
        }
        else if ( button->text() == QLatin1String( "Previous" ) ) {
            previousButton = button;
        }
    }
    REQUIRE( nextButton != nullptr );
    REQUIRE( previousButton != nullptr );

    // The first WARN line is line 4 (0-based); matches highlight in the view
    QTest::keyClicks( qfEdit, QStringLiteral( "WARN" ) );
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.currentLine().get() == 4; } ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "16_quickfind_highlight.png" ) );

    QTest::mouseClick( nextButton, Qt::LeftButton );
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.currentLine().get() == 9; } ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "17_quickfind_next.png" ) );

    QTest::mouseClick( previousButton, Qt::LeftButton );
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.currentLine().get() == 4; } ) );
}

// ---------------------------------------------------------------------------
// Dual paint path diff: the same viewport rendered through the session
// composed frame (new path) and by the view itself (old path) must come out
// pixel for pixel identical, except for the one intended behaviour fix - a
// whole line rule's back colour now fills the row (see the C5 record in
// execution-plan.md).
// ---------------------------------------------------------------------------

// One frame of the main view as painted by a given path: forces the path,
// drops the cached text pixmap and grabs the viewport.
QImage renderMainViewFrame( MainWindowFixture& fixture, bool newPath )
{
    klogg::ui::setRenderEngineOverride( newPath );
    AbstractLogView* view = fixture.crawler.mainViewWidget();
    view->forceRefresh();
    QTest::qWait( 200 );
    return fixture.crawler.mainViewViewport()->grab().toImage().convertToFormat(
        QImage::Format_RGB32 );
}

struct FrameDiff {
    int pixels = 0;
    // Viewport row -> number of differing pixels in it
    std::map<int, int> pixelsPerRow;
    int firstColumn = -1;
    int lastColumn = -1;

    bool identical() const
    {
        return pixels == 0;
    }
};

FrameDiff compareFrames( const QImage& lhs, const QImage& rhs )
{
    FrameDiff diff;
    REQUIRE( lhs.size() == rhs.size() );

    for ( int y = 0; y < lhs.height(); ++y ) {
        for ( int x = 0; x < lhs.width(); ++x ) {
            if ( lhs.pixel( x, y ) != rhs.pixel( x, y ) ) {
                ++diff.pixels;
                ++diff.pixelsPerRow[ y ];
                if ( diff.firstColumn < 0 || x < diff.firstColumn ) {
                    diff.firstColumn = x;
                }
                if ( x > diff.lastColumn ) {
                    diff.lastColumn = x;
                }
            }
        }
    }
    return diff;
}

// Renders the frame with both paths and requires them to be identical. The old
// path is rendered twice so a frame that changed between the grabs (a moving
// search, a repaint race) cannot be mistaken for a difference between the
// paths.
void requireSameFrameOnBothPaths( MainWindowFixture& fixture, const char* label )
{
    const auto oldFrame = renderMainViewFrame( fixture, false );
    const auto newFrame = renderMainViewFrame( fixture, true );
    const auto oldFrameAgain = renderMainViewFrame( fixture, false );

    INFO( "frame: " << label );
    REQUIRE( compareFrames( oldFrame, oldFrameAgain ).identical() );

    const auto diff = compareFrames( oldFrame, newFrame );
    INFO( "different pixels: " << diff.pixels );
    if ( !diff.identical() ) {
        INFO( "rows with differences: " << diff.pixelsPerRow.size() << " first row "
                                        << diff.pixelsPerRow.begin()->first );
        INFO( "columns " << diff.firstColumn << ".." << diff.lastColumn );
    }
    REQUIRE( diff.identical() );
}

// Right-click menu action that assigns a colour label to the selection
void applyColorLabel( MainWindowFixture& fixture )
{
    bool labelPicked = false;
    whenPopupMenu( [ &labelPicked ]( QMenu* popup ) {
        for ( auto* action : popup->actions() ) {
            auto* subMenu = action->menu();
            if ( subMenu == nullptr
                 || !subMenu->title().contains( QLatin1String( "Color Labels" ) ) ) {
                continue;
            }
            for ( auto* labelAction : subMenu->actions() ) {
                if ( labelAction->data().isValid() ) {
                    labelAction->trigger();
                    labelPicked = true;
                    popup->close();
                    return;
                }
            }
        }
    } );

    const auto row = static_cast<int>( 6 - fixture.crawler.crawler->getTopLine().get() );
    const auto charHeight = fixture.crawler.mainViewWidget()->fontMetrics().height();
    QTest::mouseClick(
        fixture.crawler.mainViewViewport(), Qt::RightButton, Qt::NoModifier,
        QPoint( fixture.crawler.mainViewCellPos( 6, 27 ).x(), row * charHeight + charHeight / 2 ) );
    REQUIRE( labelPicked );
    REQUIRE( waitUiState(
        [ & ] { return fixture.crawler.colorLabelsContain( QStringLiteral( "ERROR" ) ); } ) );
}

// Frame cost of both paths on the very same viewport: the text area is
// repainted and grabbed `frames` times per path and the medians are logged.
// Reported only: a loaded machine would make a threshold flaky, and the
// decisive numbers come from the perf_repro traces (execution-plan C5 record).
void reportFrameCost( MainWindowFixture& fixture, int frames )
{
    const auto measure = [ & ]( bool newPath ) {
        klogg::ui::setRenderEngineOverride( newPath );
        AbstractLogView* view = fixture.crawler.mainViewWidget();
        std::vector<qint64> micros;
        micros.reserve( static_cast<size_t>( frames ) );
        for ( int i = 0; i < frames; ++i ) {
            view->forceRefresh();
            QElapsedTimer timer;
            timer.start();
            fixture.crawler.mainViewViewport()->grab();
            micros.push_back( timer.nsecsElapsed() / 1000 );
        }
        std::sort( micros.begin(), micros.end() );
        return micros[ micros.size() / 2 ];
    };

    const auto oldMicros = measure( false );
    const auto newMicros = measure( true );
    LOG_WARNING << "dual path frame cost, median of " << frames << " grabs: old " << oldMicros
                << "us, new " << newMicros << "us, ratio "
                << static_cast<double>( newMicros ) / static_cast<double>( oldMicros );
}

SCENARIO( "Screenshots: both paint paths render the same frame", "[screenshot]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    installScreenshotHighlighters();

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx1_pathdiff_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    {
        MainWindowFixture fixture;
        REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

        // Scrolled into the results of a search: match bullets, the pattern
        // highlighters of the search and the dimmed lines outside the search
        // range are all in the same frame
        fixture.crawler.runSearchAndWait( QStringLiteral( "ERROR" ) );
        fixture.crawler.setSearchLimitsFromUi( 2000, 3000 );
        fixture.crawler.mainViewWidget()->selectAndDisplayLine( LineNumber( 2200 ) );
        QTest::qWait( 100 );
        requireSameFrameOnBothPaths( fixture, "search with dimmed lines" );

        // Text wrap off and on
        fixture.crawler.setTextWrap( false );
        fixture.crawler.mainViewWidget()->selectAndDisplayLine( LineNumber( 500 ) );
        QTest::qWait( 100 );
        requireSameFrameOnBothPaths( fixture, "wrap off" );

        fixture.crawler.setTextWrap( true );
        QTest::qWait( 100 );
        requireSameFrameOnBothPaths( fixture, "wrap on" );
        reportFrameCost( fixture, 12 );
        fixture.crawler.setTextWrap( false );

        // Selections: a whole selected line, a range of them and a selected
        // portion of one
        fixture.crawler.clickMainViewRow( 3 );
        requireSameFrameOnBothPaths( fixture, "single line selection" );
        fixture.crawler.clickMainViewRow( 6, Qt::ShiftModifier );
        requireSameFrameOnBothPaths( fixture, "line range selection" );

        fixture.crawler.mainViewWidget()->selectAndDisplayLine( LineNumber( 0 ) );
        QTest::qWait( 100 );
        requireSameFrameOnBothPaths( fixture, "no selection" );

        // Colour labels: the words come from the view, the slot colours from
        // the session
        fixture.crawler.doubleClickMainViewCell( 6, 27 );
        REQUIRE( fixture.crawler.selectionText() == QLatin1String( "ERROR" ) );
        applyColorLabel( fixture );
        requireSameFrameOnBothPaths( fixture, "colour label" );
    }

    {
        // QuickFind highlights the visible lines of the frame
        MainWindowFixture fixture;
        REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

        QTest::keyClick( fixture.window.get(), Qt::Key_F, Qt::ControlModifier );
        auto* qfWidget = fixture.window->findChild<QuickFindWidget*>();
        REQUIRE( qfWidget != nullptr );
        REQUIRE( waitUiState( [ & ] { return qfWidget->isVisible(); } ) );
        auto* qfEdit = qfWidget->findChild<QLineEdit*>();
        REQUIRE( qfEdit != nullptr );

        QTest::keyClicks( qfEdit, QStringLiteral( "WARN" ) );
        REQUIRE( waitUiState( [ & ] { return fixture.crawler.currentLine().get() == 4; } ) );
        requireSameFrameOnBothPaths( fixture, "quickfind pattern" );
    }

    {
        // A whole line rule with a back colour: the one intended difference.
        // The new path paints the row background with the rule colour, the old
        // path keeps the palette colour there, so every differing pixel must be
        // on a matching row and right of the end of its text.
        HighlighterSetCollection collection;
        auto set = HighlighterSet::createNewSet( "pixeldiff" );
        auto rule = screenshotRule( "info-lines", "INFO", "#ff3f5f8f" );
        rule.setBackColor( *klogg::Rgba::fromHex( "#ff204060" ) );
        rule.setUseThemeBack( false );
        set.setHighlighters( { rule } );
        collection.setHighlighterSets( { set } );
        collection.activateSet( set.id() );
        installScreenshotCollection( std::move( collection ) );

        MainWindowFixture fixture;
        REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

        const auto oldFrame = renderMainViewFrame( fixture, false );
        const auto newFrame = renderMainViewFrame( fixture, true );

        const auto topLine = fixture.crawler.crawler->getTopLine().get();
        const auto charHeight = fixture.crawler.mainViewWidget()->fontMetrics().height();
        const auto viewportWidth = fixture.crawler.mainViewViewport()->width();
        const auto lines = mainLogLines();
        const auto lineCount = static_cast<LineNumber::UnderlyingType>( lines.size() );

        // Lines of the frame the whole line rule repaints
        const auto matchesRule = [ & ]( LineNumber::UnderlyingType line ) {
            return line < lineCount
                   && lines[ static_cast<int>( line ) ].contains( QLatin1String( "[INFO]" ) );
        };

        std::set<LineNumber::UnderlyingType> expectedLines;
        for ( int row = 0; row * charHeight < oldFrame.height(); ++row ) {
            const auto line = topLine + static_cast<LineNumber::UnderlyingType>( row );
            if ( matchesRule( line ) ) {
                expectedLines.insert( line );
            }
        }
        REQUIRE( expectedLines.empty() == false );

        const auto diff = compareFrames( oldFrame, newFrame );
        INFO( "different pixels: " << diff.pixels << " in " << diff.pixelsPerRow.size()
                                   << " pixel rows" );

        // The fix has to be visible, and only on the lines the rule repaints
        REQUIRE( diff.identical() == false );
        std::set<LineNumber::UnderlyingType> touchedLines;
        for ( const auto& [ pixelRow, count ] : diff.pixelsPerRow ) {
            const auto line = topLine + static_cast<LineNumber::UnderlyingType>( pixelRow / charHeight );
            INFO( "pixel row " << pixelRow << " (line " << line << ") has " << count
                               << " changed pixels" );
            touchedLines.insert( line );
        }
        REQUIRE( touchedLines == expectedLines );

        // The row background changes right of the text; the text itself, the
        // gutter, the bullets and the line numbers are untouched. The 2 pixel
        // slack is the cell rounding of the helper.
        const auto contentStartX
            = fixture.crawler.mainViewCellPos( topLine, 0 ).x()
              - fixture.crawler.mainViewWidget()->fontMetrics().horizontalAdvance(
                  QLatin1Char( 'm' ) )
              - 2;
        INFO( "content starts at " << contentStartX << ", first changed column " << diff.firstColumn
                                   << ", last " << diff.lastColumn );
        REQUIRE( diff.firstColumn >= contentStartX );
        REQUIRE( diff.lastColumn == viewportWidth - 1 );
    }

    // Back to the highlighters the other scenarios expect
    installScreenshotHighlighters();
    // Production default
    klogg::ui::setRenderEngineOverride( std::nullopt );
}

SCENARIO( "Screenshots: go to line", "[screenshot][search]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx1_goto_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    // Answer the modal "Jump to line" dialog from inside its event loop
    QTimer::singleShot( 500, qApp, [] {
        auto* dialog = qobject_cast<QInputDialog*>( QApplication::activeModalWidget() );
        if ( dialog == nullptr ) {
            return;
        }
        auto* edit = dialog->findChild<QLineEdit*>();
        if ( edit != nullptr ) {
            edit->setText( QStringLiteral( "2500" ) );
            QTest::keyClick( edit, Qt::Key_Return );
        }
    } );
    QTest::keyClick( fixture.window.get(), Qt::Key_L, Qt::ControlModifier );

    // Line numbers in the dialog are 1-based: 2500 selects line 2499
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.currentLine().get() == 2499; } ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "18_goto_line.png" ) );
}

SCENARIO( "Screenshots: range selection and copy to clipboard", "[screenshot][search]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx1_copy_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    // Click line 10, Shift+click line 14: five lines selected
    fixture.crawler.clickMainViewRow( 10 );
    fixture.crawler.clickMainViewRow( 14, Qt::ShiftModifier );

    const auto selectedText = fixture.crawler.selectionText();
    REQUIRE( selectedText.contains( QStringLiteral( "requestId=10" ) ) );
    REQUIRE( selectedText.contains( QStringLiteral( "requestId=14" ) ) );

    QTest::keyClick( fixture.window.get(), Qt::Key_C, Qt::ControlModifier );
    QTest::qWait( 100 );

    const auto clipboardText = QGuiApplication::clipboard()->text();
    REQUIRE( clipboardText.contains( QStringLiteral( "requestId=10" ) ) );
    REQUIRE( clipboardText.contains( QStringLiteral( "requestId=14" ) ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "19_select_copy.png" ) );
}

SCENARIO( "Screenshots: overview click navigation", "[screenshot][search]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx1_overview_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    // Matches paint the overview strip with marks
    fixture.crawler.runSearchAndWait( QStringLiteral( "ERROR" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );

    // The queued search-completion callback re-syncs the selection to the
    // first match (line 6) and pulls the main view back to it; let it land
    // before navigating, or it fires after the click and undoes the jump
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.currentLine().get() == 6; } ) );

    auto* overview = fixture.crawler.overview();
    REQUIRE( overview != nullptr );
    REQUIRE( overview->isVisible() );
    REQUIRE( fixture.crawler.crawler->getTopLine().get() == 0 );

    QTest::mouseClick( overview, Qt::LeftButton, Qt::NoModifier,
                       QPoint( overview->width() / 2, overview->height() - 8 ) );
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.crawler->getTopLine().get() > 2000; } ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "20_overview_nav_bottom.png" ) );

    QTest::mouseClick( overview, Qt::LeftButton, Qt::NoModifier,
                       QPoint( overview->width() / 2, 8 ) );
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.crawler->getTopLine().get() < 500; } ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "21_overview_nav_top.png" ) );
}

SCENARIO( "Screenshots: search limited to a line range", "[screenshot][search]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx1_limits_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    fixture.crawler.runSearchAndWait( QStringLiteral( "ERROR" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );

    // 143 ERROR lines (i % 7 == 6) fall inside [1000, 2000)
    fixture.crawler.setSearchLimitsFromUi( 1000, 2000 );
    fixture.crawler.runSearchAndWait( QStringLiteral( "ERROR" ) );
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == 143; } ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "22_search_limits.png" ) );

    fixture.crawler.clearSearchLimitsFromUi();
    fixture.crawler.runSearchAndWait( QStringLiteral( "ERROR" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );
}

SCENARIO( "Screenshots: clear search returns to the marks projection", "[screenshot][search]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx1_clear_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    fixture.crawler.runSearchAndWait( QStringLiteral( "ERROR" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );

    // A marked non-matching line joins the live union, then survives the clear
    fixture.crawler.clickGutterRow( 3 );
    REQUIRE( waitUiState(
        [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors + 1; } ) );

    fixture.crawler.clearSearchTextAndRun();
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == 1; } ) );
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "23_clear_search.png" ) );
}

// ---- State and dialogs coverage ----

SCENARIO( "Screenshots: highlighters dialog adds a rule", "[screenshot][state]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    // A single empty active set isolates the rule added through the dialog
    {
        HighlighterSetCollection collection;
        auto emptySet = HighlighterSet::createNewSet( "fx2-dialog" );
        collection.setHighlighterSets( { emptySet } );
        collection.activateSet( emptySet.id() );
        installScreenshotCollection( std::move( collection ) );
    }

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx2_hl_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    bool dialogDriven = false;
    whenModalDialog<HighlightersDialog>(
        [ screenshotDir, &dialogDriven ]( HighlightersDialog* dialog ) {
            auto* setEdit = dialog->findChild<HighlighterSetEdit*>();
            if ( setEdit == nullptr ) {
                return;
            }
            // The first set row is selected from the dialog ctor via a posted call
            if ( !waitInModal( [ & ] {
                     return setEdit->nameEdit->text() == QLatin1String( "fx2-dialog" );
                 } ) ) {
                return;
            }
            QTest::mouseClick( setEdit->addHighlighterButton, Qt::LeftButton );
            QTest::qWait( 50 );

            auto* ruleEdit = setEdit->findChild<HighlighterEdit*>();
            if ( ruleEdit == nullptr ) {
                return;
            }
            ruleEdit->patternEdit->clear();
            QTest::keyClicks( ruleEdit->patternEdit, QStringLiteral( "ERROR" ) );
            QTest::qWait( 50 );

            // The default rule has no back color, so the picker button is
            // disabled until "Use theme" is unticked
            if ( ruleEdit->useThemeBackCheckBox->isChecked() ) {
                ruleEdit->useThemeBackCheckBox->click();
                QTest::qWait( 50 );
            }
            whenModalDialog<QColorDialog>( []( QColorDialog* picker ) {
                picker->setCurrentColor( QColor( QStringLiteral( "#ff33cc" ) ) );
                picker->accept();
            } );
            QTest::mouseClick( ruleEdit->backColorButton, Qt::LeftButton );
            QTest::qWait( 100 );

            saveShot( dialog, screenshotDir, "24_highlighters_dialog.png" );
            QTest::mouseClick( dialog->buttonBox->button( QDialogButtonBox::Ok ), Qt::LeftButton );
            dialogDriven = true;
        } );

    auto* highlightersMenu = findHighlightersMenu( fixture.window.get() );
    REQUIRE( highlightersMenu != nullptr );
    auto* configureAction
        = findActionByText( highlightersMenu->actions(), QStringLiteral( "highlighters" ) );
    REQUIRE( configureAction != nullptr );
    configureAction->trigger();
    REQUIRE( dialogDriven );

    const auto sets = klogg::ui::HighlighterCache::instance().collection().highlighterSets();
    REQUIRE( sets.size() == 1 );
    const auto& rules = sets.front().highlighters();
    REQUIRE( std::any_of( rules.begin(), rules.end(), []( const auto& rule ) {
        return rule.pattern() == "ERROR"
               && rule.backColor() == *klogg::Rgba::fromHex( "#ffff33cc" );
    } ) );
    REQUIRE( waitUiState( [] { return activeHighlighterCacheHasPattern( "ERROR" ); } ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "25_highlighter_applied.png" ) );
}

SCENARIO( "Screenshots: highlighter set switch recolors the view", "[screenshot][state]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    installScreenshotHighlighters();

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx2_hlsw_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    bool dialogDriven = false;
    whenModalDialog<HighlightersDialog>( [ &dialogDriven ]( HighlightersDialog* dialog ) {
        QTest::mouseClick( dialog->addHighlighterButton, Qt::LeftButton );

        auto* setEdit = dialog->findChild<HighlighterSetEdit*>();
        if ( setEdit == nullptr ) {
            return;
        }
        // The new set row is selected via a posted call
        if ( !waitInModal( [ & ] {
                 return setEdit->nameEdit->text() == QLatin1String( "New Highlighter set" );
             } ) ) {
            return;
        }
        setEdit->nameEdit->clear();
        QTest::keyClicks( setEdit->nameEdit, QStringLiteral( "fx2-beta" ) );

        QTest::mouseClick( setEdit->addHighlighterButton, Qt::LeftButton );
        QTest::qWait( 50 );
        auto* ruleEdit = setEdit->findChild<HighlighterEdit*>();
        if ( ruleEdit == nullptr ) {
            return;
        }
        ruleEdit->patternEdit->clear();
        QTest::keyClicks( ruleEdit->patternEdit, QStringLiteral( "WARN" ) );
        QTest::qWait( 50 );

        // The default rule has no back color, so the picker button is
        // disabled until "Use theme" is unticked
        if ( ruleEdit->useThemeBackCheckBox->isChecked() ) {
            ruleEdit->useThemeBackCheckBox->click();
            QTest::qWait( 50 );
        }
        whenModalDialog<QColorDialog>( []( QColorDialog* picker ) {
            picker->setCurrentColor( QColor( QStringLiteral( "#ff8800" ) ) );
            picker->accept();
        } );
        QTest::mouseClick( ruleEdit->backColorButton, Qt::LeftButton );
        QTest::qWait( 100 );

        QTest::mouseClick( dialog->buttonBox->button( QDialogButtonBox::Ok ), Qt::LeftButton );
        dialogDriven = true;
    } );

    auto* highlightersMenu = findHighlightersMenu( fixture.window.get() );
    REQUIRE( highlightersMenu != nullptr );
    auto* configureAction
        = findActionByText( highlightersMenu->actions(), QStringLiteral( "highlighters" ) );
    REQUIRE( configureAction != nullptr );
    configureAction->trigger();
    REQUIRE( dialogDriven );

    const auto setsAfterDialog
        = klogg::ui::HighlighterCache::instance().collection().highlighterSets();
    REQUIRE( setsAfterDialog.size() == 2 );

    // Only the base set is active: WARN lines are still plain
    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "26_highlighter_set_before.png" ) );

    auto ensureSetActive = [ highlightersMenu ]( const QString& name, bool wantActive ) {
        auto& collection = klogg::ui::HighlighterCache::instance().collection();
        const auto sets = collection.highlighterSets();
        const auto it = std::find_if( sets.begin(), sets.end(), [ & ]( const auto& set ) {
            return set.name() == name.toStdString();
        } );
        REQUIRE( it != sets.end() );
        const auto activeIds = collection.activeSetIds();
        const auto isActive = std::find( activeIds.begin(), activeIds.end(), it->id() )
                              != activeIds.end();
        if ( isActive == wantActive ) {
            return;
        }
        auto* action = findActionByText( highlightersMenu->actions(), name );
        REQUIRE( action != nullptr );
        action->trigger();
    };
    ensureSetActive( QStringLiteral( "fx2-beta" ), true );
    ensureSetActive( QStringLiteral( "screenshot" ), false );

    REQUIRE( waitUiState( [] {
        return activeHighlighterCacheHasPattern( "WARN" )
               && !activeHighlighterCacheHasPattern( "ERROR" );
    } ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "27_highlighter_set_switched.png" ) );
}

SCENARIO( "Screenshots: quick highlight a selection via the context menu", "[screenshot][state]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    installScreenshotHighlighters();

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx2_qhl_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    // Double-click selects the whole word as a portion; "ERROR" sits at
    // column 25 of line 6 (the first ERROR line)
    fixture.crawler.doubleClickMainViewCell( 6, 27 );
    REQUIRE( fixture.crawler.selectionText() == QLatin1String( "ERROR" ) );

    bool labelPicked = false;
    whenPopupMenu( [ &labelPicked ]( QMenu* popup ) {
        for ( auto* action : popup->actions() ) {
            auto* subMenu = action->menu();
            if ( subMenu == nullptr
                 || !subMenu->title().contains( QLatin1String( "Color Labels" ) ) ) {
                continue;
            }
            for ( auto* labelAction : subMenu->actions() ) {
                if ( labelAction->data().isValid() ) {
                    labelAction->trigger();
                    labelPicked = true;
                    popup->close();
                    return;
                }
            }
        }
    } );

    // Right-click inside the portion keeps the selection (a click outside
    // would collapse it to a whole-line selection)
    fixture.crawler.rightClickMainViewCell( 6, 27 );
    REQUIRE( labelPicked );
    REQUIRE( waitUiState(
        [ & ] { return fixture.crawler.colorLabelsContain( QStringLiteral( "ERROR" ) ); } ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "28_quick_highlight.png" ) );
}

SCENARIO( "Screenshots: predefined filters management and apply", "[screenshot][state]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    // Isolated filter list; clear the search history too: updateSearchCombo
    // transiently exposes the latest history entry as the combo text while
    // repopulating, and the popup syncs its checks against that pattern
    klogg::ui::predefinedFilters().setFilters( {} );
    klogg::ui::savePredefinedFilters();
    klogg::ui::searchHistory().clear();
    klogg::ui::saveSearchHistory();

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx2_pf_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    bool dialogDriven = false;
    whenModalDialog<PredefinedFiltersDialog>(
        [ screenshotDir, &dialogDriven ]( PredefinedFiltersDialog* dialog ) {
            QTest::mouseClick( dialog->addFilterButton, Qt::LeftButton );
            QTest::qWait( 100 );
            QTest::keyClicks( QApplication::focusWidget(), QStringLiteral( "errors" ) );
            QTest::keyClick( QApplication::focusWidget(), Qt::Key_Return );
            QTest::qWait( 50 );

            auto* table = dialog->filtersTableWidget;
            table->setCurrentCell( 0, 1 );
            table->editItem( table->item( 0, 1 ) );
            QTest::qWait( 50 );
            QTest::keyClicks( QApplication::focusWidget(), QStringLiteral( "ERROR" ) );
            QTest::keyClick( QApplication::focusWidget(), Qt::Key_Return );
            QTest::qWait( 50 );

            saveShot( dialog, screenshotDir, "29_predefined_filters_dialog.png" );
            QTest::mouseClick( dialog->buttonBox->button( QDialogButtonBox::Ok ), Qt::LeftButton );
            dialogDriven = true;
        } );

    auto* toolsMenu = findTopMenu( fixture.window.get(), QStringLiteral( "Tools" ) );
    REQUIRE( toolsMenu != nullptr );
    auto* filtersAction
        = findActionByText( toolsMenu->actions(), QStringLiteral( "Predefined filters" ) );
    REQUIRE( filtersAction != nullptr );
    filtersAction->trigger();
    REQUIRE( dialogDriven );

    REQUIRE( waitUiState( [] {
        const auto& filters = klogg::ui::predefinedFilters().getFilters();
        return filters.size() == 1 && filters.front().pattern == "ERROR";
    } ) );

    auto* combo = fixture.crawler.predefinedFiltersCombo();
    REQUIRE( waitUiState( [ & ] { return combo->count() >= 2; } ) );

    combo->showPopup();
    QTest::qWait( 100 );
    REQUIRE( saveShot( combo->view()->window(), screenshotDir, "30_predefined_filter_popup.png" ) );

    // Row 0 is the title; tick the filter with a real click on the popup row
    auto* comboView = combo->view();
    const auto filterIndex = comboView->model()->index( 1, 0 );
    QTest::mouseClick( comboView->viewport(), Qt::LeftButton, Qt::NoModifier,
                       comboView->visualRect( filterIndex ).center() );
    QTest::qWait( 100 );
    if ( combo->view()->window()->isVisible() ) {
        combo->hidePopup();
    }

    REQUIRE( waitUiState( [ & ] {
        return fixture.crawler.searchCombo()->currentText().contains( QLatin1String( "ERROR" ) );
    } ) );

    fixture.crawler.clickSearchAndWait();
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "31_predefined_filter_applied.png" ) );
}

SCENARIO( "Screenshots: search history dropdown", "[screenshot][state]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    klogg::ui::searchHistory().clear();
    klogg::ui::saveSearchHistory();

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx2_hist_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    fixture.crawler.runSearchAndWait( QStringLiteral( "ERROR" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );
    fixture.crawler.runSearchAndWait( QStringLiteral( "WARN" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedWarns; } ) );
    fixture.crawler.runSearchAndWait( QStringLiteral( "DEBUG" ) );

    auto* combo = fixture.crawler.searchCombo();
    REQUIRE( waitUiState( [ & ] { return combo->count() >= 3; } ) );

    combo->showPopup();
    QTest::qWait( 100 );
    REQUIRE( saveShot( combo->view()->window(), screenshotDir, "32_search_history_popup.png" ) );
    combo->hidePopup();
}

SCENARIO( "Screenshots: mark keyboard navigation and clear", "[screenshot][state]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx2_marks_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    // Marks on lines 3, 10 and 20; all fit on the first screen
    fixture.crawler.clickGutterRow( 3 );
    fixture.crawler.clickGutterRow( 10 );
    fixture.crawler.clickGutterRow( 20 );

    const auto fileId = fixture.crawler.crawler->fileId();
    REQUIRE( waitUiState( [ & ] {
        const auto marks = klogg::ui::apiSession().listMarks( fileId );
        return marks && marks->size() == 3;
    } ) );

    fixture.crawler.clickMainViewRow( 0 );
    fixture.crawler.focusMainView();

    QTest::keyClick( fixture.crawler.mainView(), Qt::Key_BracketRight );
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.currentLine().get() == 3; } ) );
    QTest::keyClick( fixture.crawler.mainView(), Qt::Key_BracketRight );
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.currentLine().get() == 10; } ) );
    QTest::keyClick( fixture.crawler.mainView(), Qt::Key_BracketRight );
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.currentLine().get() == 20; } ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "33_marks_nav.png" ) );

    QTest::keyClick( fixture.crawler.mainView(), Qt::Key_BracketLeft );
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.currentLine().get() == 10; } ) );

    // Gutter-clicking a marked line unmarks it
    fixture.crawler.clickGutterRow( 3 );
    fixture.crawler.clickGutterRow( 10 );
    fixture.crawler.clickGutterRow( 20 );
    REQUIRE( waitUiState( [ & ] {
        const auto marks = klogg::ui::apiSession().listMarks( fileId );
        return marks && marks->empty();
    } ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "34_marks_cleared.png" ) );
}

SCENARIO( "Screenshots: send selection to scratchpad", "[screenshot][state]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx2_pad_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    fixture.crawler.clickMainViewRow( 10 );
    fixture.crawler.clickMainViewRow( 14, Qt::ShiftModifier );
    REQUIRE( fixture.crawler.selectionText().contains( QStringLiteral( "requestId=10" ) ) );

    bool sendPicked = false;
    whenPopupMenu( [ &sendPicked ]( QMenu* popup ) {
        if ( auto* action = findActionByText( popup->actions(), QStringLiteral( "scratchpad" ) ) ) {
            action->trigger();
            sendPicked = true;
            popup->close();
        }
    } );

    // Right-click inside the selected range keeps it
    const auto charHeight = fixture.crawler.mainView()->fontMetrics().height();
    QTest::mouseClick( fixture.crawler.mainViewViewport(), Qt::RightButton, Qt::NoModifier,
                       QPoint( fixture.crawler.mainViewViewport()->width() / 2,
                               12 * charHeight + charHeight / 2 ) );
    REQUIRE( sendPicked );

    REQUIRE( waitUiState( [] {
        auto* scratchPad = findScratchPadWindow();
        return scratchPad != nullptr && scratchPad->isVisible();
    } ) );

    auto* scratchPad = findScratchPadWindow();
    REQUIRE( scratchPad != nullptr );
    auto* tabs = scratchPad->findChild<QTabWidget*>();
    REQUIRE( tabs != nullptr );
    auto* padText = tabs->currentWidget()->findChild<QPlainTextEdit*>();
    REQUIRE( padText != nullptr );
    REQUIRE( waitUiState( [ & ] {
        const auto text = padText->toPlainText();
        return text.contains( QStringLiteral( "requestId=10" ) )
               && text.contains( QStringLiteral( "requestId=14" ) );
    } ) );

    REQUIRE( saveShot( scratchPad, screenshotDir, "35_scratchpad.png" ) );
}

SCENARIO( "Screenshots: options dialog", "[screenshot][state]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx2_opts_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    bool dialogDriven = false;
    whenModalDialog<OptionsDialog>( [ screenshotDir, &dialogDriven ]( OptionsDialog* dialog ) {
        QTest::qWait( 100 );
        saveShot( dialog, screenshotDir, "36_options_dialog.png" );
        QTest::mouseClick( dialog->buttonBox->button( QDialogButtonBox::Cancel ), Qt::LeftButton );
        dialogDriven = true;
    } );

    auto* fileMenu = findTopMenu( fixture.window.get(), QStringLiteral( "File" ) );
    REQUIRE( fileMenu != nullptr );
    auto* optionsAction = findActionByText( fileMenu->actions(), QStringLiteral( "Preferences" ) );
    REQUIRE( optionsAction != nullptr );
    optionsAction->trigger();
    REQUIRE( dialogDriven );
}

// ---- File behavior and event coverage ----

SCENARIO( "Screenshots: follow mode picks up appended lines", "[screenshot][file]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx3_follow_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    auto* viewMenu = findTopMenu( fixture.window.get(), QStringLiteral( "View" ) );
    REQUIRE( viewMenu != nullptr );
    auto* followAction = findActionByText( viewMenu->actions(), QStringLiteral( "Follow" ) );
    REQUIRE( followAction != nullptr );
    followAction->trigger();
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.isFollowEnabled(); } ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "37_follow_before.png" ) );

    QStringList extraLines;
    for ( qulonglong i = 0; i < 50; ++i ) {
        extraLines.append( QStringLiteral( "2024-01-15 10:03:05.%1 [INFO] [thread-00] Appended "
                                           "after follow tailId=%2" )
                               .arg( i, 3, 10, QLatin1Char( '0' ) )
                               .arg( kMainLogLines + i ) );
    }
    appendLines( file.fileName(), extraLines );

    // The native directory watch arms asynchronously on the watcher thread; an
    // append racing its startup is lost, so prod the file again if the first
    // change went unnoticed
    auto grew = waitUiState( [ & ] { return fixture.crawler.mainLines().get() > kMainLogLines; } );
    for ( int attempt = 0; attempt < 3 && !grew; ++attempt ) {
        appendLines( file.fileName(),
                     { QStringLiteral( "2024-01-15 10:03:06.000 [INFO] [thread-00] Appended "
                                       "after follow retry" ) } );
        grew = waitUiState( [ & ] { return fixture.crawler.mainLines().get() > kMainLogLines; } );
    }
    REQUIRE( grew );
    // Follow mode keeps the view pinned to the tail
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.crawler->getTopLine().get() > 0; } ) );
    REQUIRE( fixture.crawler.mainViewLineText( fixture.crawler.mainLines().get() - 1 )
                 .contains( QLatin1String( "Appended" ) ) );
    // Follow mode keeps the view pinned to the tail
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.crawler->getTopLine().get() > 0; } ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "38_follow_after_append.png" ) );
}

SCENARIO( "Screenshots: file truncated on disk resets marks and search", "[screenshot][file]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx3_trunc_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    fixture.crawler.runSearchAndWait( QStringLiteral( "ERROR" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );

    fixture.crawler.clickGutterRow( 3 );
    fixture.crawler.clickGutterRow( 8 );
    const auto fileId = fixture.crawler.crawler->fileId();
    REQUIRE( waitUiState( [ & ] {
        const auto marks = klogg::ui::apiSession().listMarks( fileId );
        return marks && marks->size() == 2;
    } ) );

    // Cut the file down to a fresh, much shorter content
    QStringList shortLines;
    for ( int i = 0; i < 100; ++i ) {
        shortLines.append(
            QStringLiteral( "2024-01-15 11:00:00.%1 [INFO] fresh file after truncate line %2" )
                .arg( i, 3, 10, QLatin1Char( '0' ) )
                .arg( i ) );
    }
    rewriteLines( file.fileName(), shortLines );

    REQUIRE( waitUiState( [ & ] { return fixture.crawler.mainLines().get() == 100; } ) );
    // Marks are dropped and the interrupted search leaves the truncated notice
    REQUIRE( waitUiState( [ & ] {
        const auto marks = klogg::ui::apiSession().listMarks( fileId );
        return marks && marks->empty();
    } ) );
    REQUIRE( waitUiState( [ & ] {
        return fixture.crawler.searchInfoText().contains( QLatin1String( "truncated" ),
                                                          Qt::CaseInsensitive );
    } ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "39_file_truncated.png" ) );
}

SCENARIO( "Screenshots: reload picks up replaced content", "[screenshot][file]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx3_reload_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    QStringList newLines;
    for ( int i = 0; i < 120; ++i ) {
        newLines.append( QStringLiteral( "2024-01-15 12:00:00.%1 [INFO] RELOADED_MARKER line %2" )
                             .arg( i, 3, 10, QLatin1Char( '0' ) )
                             .arg( i ) );
    }
    rewriteLines( file.fileName(), newLines );

    auto* viewMenu = findTopMenu( fixture.window.get(), QStringLiteral( "View" ) );
    REQUIRE( viewMenu != nullptr );
    auto* reloadAction = findActionByText( viewMenu->actions(), QStringLiteral( "Reload" ) );
    REQUIRE( reloadAction != nullptr );
    reloadAction->trigger();

    REQUIRE( waitUiState( [ & ] { return fixture.crawler.mainLines().get() == 120; } ) );
    REQUIRE( waitUiState( [ & ] {
        return fixture.crawler.mainViewLineText( 0 ).contains( QLatin1String( "RELOADED_MARKER" ) );
    } ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "40_reload_replaced.png" ) );
}

SCENARIO( "Screenshots: same file in two windows with independent searches", "[screenshot][file]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx3_multi_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    fixture.crawler.runSearchAndWait( QStringLiteral( "ERROR" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );

    // The app-level handler for this signal lives in KloggApplication, which the
    // test process does not run; mirror exactly what it does
    std::unique_ptr<MainWindow> secondWindow;
    QObject::connect( fixture.window.get(), &MainWindow::openFileInNewWindow, fixture.window.get(),
                      [ & ]( const QString& fileName, uint64_t topLine, const QString& viewContext,
                             const QString& searchText ) {
                          secondWindow = std::make_unique<MainWindow>(
                              WindowSession{ fixture.appSession, "Screenshot", 0 } );
                          secondWindow->resize( 1280, 800 );
                          secondWindow->show();
                          secondWindow->loadFileFromAnotherWindow( fileName, topLine, viewContext,
                                                                   searchText );
                      } );

    auto* fileMenu = findTopMenu( fixture.window.get(), QStringLiteral( "File" ) );
    REQUIRE( fileMenu != nullptr );
    auto* openInNewWindowAction
        = findActionByText( fileMenu->actions(), QStringLiteral( "Open in new window" ) );
    REQUIRE( openInNewWindowAction != nullptr );
    openInNewWindowAction->trigger();

    REQUIRE( waitUiState( [ & ] { return secondWindow != nullptr; } ) );

    auto* tabs2 = secondWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabs2 != nullptr );
    REQUIRE( waitUiState( [ & ] { return tabs2->count() == 1; } ) );

    CrawlerWidget::access_by<ScreenshotAccess> crawler2;
    crawler2.crawler = qobject_cast<CrawlerWidget*>( tabs2->widget( 0 ) );
    REQUIRE( crawler2.crawler != nullptr );
    REQUIRE( waitUiState( [ & ] { return crawler2.mainLines().get() == kMainLogLines; } ) );
    REQUIRE( waitUiState( [ & ] { return crawler2.isLoadingFinished(); } ) );

    // Same underlying file through the shared session
    REQUIRE( crawler2.crawler->fileId() == fixture.crawler.crawler->fileId() );

    // The new window inherits the search text; replace it with its own search
    crawler2.runSearchAndWait( QStringLiteral( "WARN" ) );
    REQUIRE( waitUiState( [ & ] { return crawler2.filteredLines().get() == kExpectedWarns; } ) );
    // The first window keeps its own result set
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "41_multiwindow_first.png" ) );
    REQUIRE( saveShot( secondWindow.get(), screenshotDir, "42_multiwindow_second.png" ) );
}

SCENARIO( "Screenshots: event category filter narrows the events view", "[screenshot][file]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    removeAllLogTypeConfigs();
    REQUIRE( klogg::ui::replaceLogTypeConfigs( { makeEventLogType() } ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx3_cat_XXXXXX.shotlog" ) };
    writeLines( file, eventLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kEventLogLines ) );

    fixture.crawler.showEvents();
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.eventsPopulated(); }, 30000 ) );

    const auto allEventsCount = fixture.crawler.filteredLines().get();
    REQUIRE( allEventsCount > 0 );

    QToolButton* categoryButton = nullptr;
    for ( auto* button : fixture.window->findChildren<QToolButton*>() ) {
        if ( button->text().contains( QLatin1String( "Categories" ) )
             && button->menu() != nullptr ) {
            categoryButton = button;
            break;
        }
    }
    REQUIRE( categoryButton != nullptr );

    QAction* dbAction = nullptr;
    for ( auto* action : categoryButton->menu()->actions() ) {
        if ( action->data().toString() == QLatin1String( "db" ) ) {
            dbAction = action;
            break;
        }
    }
    REQUIRE( dbAction != nullptr );
    dbAction->trigger();

    // The db category contributes the 3 transaction begin/end line pairs
    REQUIRE( waitUiState(
        [ & ] { return fixture.crawler.filteredLines().get() == allEventsCount - 6; } ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "43_event_category_filtered.png" ) );

    removeAllLogTypeConfigs();
}

SCENARIO( "Screenshots: export events to Chrome trace JSON", "[screenshot][file]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    // The save-file dialog is native by default and cannot be driven offscreen
    QApplication::setAttribute( Qt::AA_DontUseNativeDialogs, true );

    removeAllLogTypeConfigs();
    REQUIRE( klogg::ui::replaceLogTypeConfigs( { makeEventLogType() } ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx3_export_XXXXXX.shotlog" ) };
    writeLines( file, eventLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kEventLogLines ) );

    fixture.crawler.showEvents();
    REQUIRE( waitUiState( [ & ] { return fixture.crawler.eventsPopulated(); }, 30000 ) );

    const QString exportName = QStringLiteral( "klogg_fx3_events_%1.json" )
                                   .arg( QUuid::createUuid().toString( QUuid::WithoutBraces ) );
    const QString exportPath = QDir::temp().filePath( exportName );

    armSaveDialogAnswer( QDir::temp().path(), exportName );
    // The success notification is modal too
    bool exportAcknowledged = false;
    whenModalDialog<QMessageBox>( [ &exportAcknowledged ]( QMessageBox* box ) {
        box->accept();
        exportAcknowledged = true;
    } );

    auto* toolsMenu = findTopMenu( fixture.window.get(), QStringLiteral( "Tools" ) );
    REQUIRE( toolsMenu != nullptr );
    auto* exportAction
        = findActionByText( toolsMenu->actions(), QStringLiteral( "Export Events" ) );
    REQUIRE( exportAction != nullptr );
    exportAction->trigger();

    REQUIRE( waitUiState( [ & ] { return QFileInfo::exists( exportPath ); } ) );
    REQUIRE( waitUiState( [ & ] { return exportAcknowledged; } ) );

    QFile exported{ exportPath };
    REQUIRE( exported.open( QIODevice::ReadOnly ) );
    const auto doc = QJsonDocument::fromJson( exported.readAll() );
    REQUIRE( doc.isObject() );
    const auto traceEvents = doc.object().value( QLatin1String( "traceEvents" ) ).toArray();
    REQUIRE( !traceEvents.isEmpty() );
    const auto firstEvent = traceEvents.first().toObject();
    REQUIRE( firstEvent.contains( QLatin1String( "ph" ) ) );
    REQUIRE( firstEvent.contains( QLatin1String( "ts" ) ) );
    REQUIRE( firstEvent.contains( QLatin1String( "name" ) ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "44_events_exported.png" ) );

    QFile::remove( exportPath );
    QApplication::setAttribute( Qt::AA_DontUseNativeDialogs, false );
    removeAllLogTypeConfigs();
}

SCENARIO( "Screenshots: mark selection as event creates a rule", "[screenshot][file]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    removeAllLogTypeConfigs();

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx3_mke_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    fixture.crawler.clickMainViewRow( 6 );
    REQUIRE( fixture.crawler.selectionText().contains( QLatin1String( "requestId=6" ) ) );

    bool markPicked = false;
    whenPopupMenu( [ &markPicked ]( QMenu* popup ) {
        if ( auto* action
             = findActionByText( popup->actions(), QStringLiteral( "Mark as Event" ) ) ) {
            action->trigger();
            markPicked = true;
            popup->close();
        }
    } );
    fixture.crawler.rightClickMainViewCell( 6, 40 );
    REQUIRE( markPicked );

    // The dialog is non-modal; wait for it among the top-level widgets
    REQUIRE( waitUiState( [] { return findTopLevelWidget<EventConfigDialog>() != nullptr; } ) );
    auto* dialog = findTopLevelWidget<EventConfigDialog>();
    REQUIRE( dialog != nullptr );

    const auto combos = dialog->findChildren<QComboBox*>();
    REQUIRE( !combos.isEmpty() );
    QTest::keyClicks( combos.first()->lineEdit(), QStringLiteral( "fx3-event" ) );
    QTest::qWait( 100 );
    // onAccept pops a modal warning on validation errors; catch a missing name
    // here instead of hanging on that box
    REQUIRE( combos.first()->currentText() == QLatin1String( "fx3-event" ) );

    REQUIRE( saveShot( dialog, screenshotDir, "45_mark_as_event_dialog.png" ) );

    // Dismiss any validation warning so it cannot block the run
    whenModalDialog<QMessageBox>( []( QMessageBox* box ) { box->accept(); } );
    auto* okButton = dialog->findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Ok );
    REQUIRE( okButton != nullptr );
    QTest::mouseClick( okButton, Qt::LeftButton );

    REQUIRE( waitUiState( [] {
        const auto rules = klogg::ui::allEventRules();
        return std::any_of( rules.begin(), rules.end(),
                            []( const EventRule& rule ) { return rule.name == "fx3-event"; } );
    } ) );

    REQUIRE( klogg::ui::removeEventRule( QStringLiteral( "fx3-event" ) ) );
    removeAllLogTypeConfigs();
}

SCENARIO( "Screenshots: event rules manager and log parsing rules dialogs", "[screenshot][file]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    removeAllLogTypeConfigs();
    REQUIRE( klogg::ui::replaceLogTypeConfigs( { makeEventLogType() } ) );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx3_dlg_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    auto* toolsMenu = findTopMenu( fixture.window.get(), QStringLiteral( "Tools" ) );
    REQUIRE( toolsMenu != nullptr );

    auto* manageRulesAction
        = findActionByText( toolsMenu->actions(), QStringLiteral( "Manage Event Rules" ) );
    REQUIRE( manageRulesAction != nullptr );
    manageRulesAction->trigger();
    REQUIRE( waitUiState(
        [] { return findTopLevelDialogByTitle( QStringLiteral( "Manage Event Rules" ) ); } ) );
    auto* rulesDialog = findTopLevelDialogByTitle( QStringLiteral( "Manage Event Rules" ) );
    REQUIRE( rulesDialog != nullptr );
    REQUIRE( saveShot( rulesDialog, screenshotDir, "46_event_rules_manager.png" ) );
    rulesDialog->close();
    REQUIRE( waitUiState( [] {
        return findTopLevelDialogByTitle( QStringLiteral( "Manage Event Rules" ) ) == nullptr;
    } ) );

    auto* parsingRulesAction
        = findActionByText( toolsMenu->actions(), QStringLiteral( "Log Parsing Rules" ) );
    REQUIRE( parsingRulesAction != nullptr );
    parsingRulesAction->trigger();
    REQUIRE( waitUiState( [] { return findTopLevelWidget<LogParsingRulesDialog>() != nullptr; } ) );
    auto* parsingDialog = findTopLevelWidget<LogParsingRulesDialog>();
    REQUIRE( parsingDialog != nullptr );
    REQUIRE( saveShot( parsingDialog, screenshotDir, "47_log_parsing_rules.png" ) );
    parsingDialog->close();

    removeAllLogTypeConfigs();
}

SCENARIO( "Screenshots: save filtered results to file", "[screenshot][file]" )
{
    const auto screenshotDir = qEnvironmentVariable( "KLOGG_SCREENSHOT_DIR" );
    if ( screenshotDir.isEmpty() ) {
        QSKIP( "KLOGG_SCREENSHOT_DIR is not set" );
    }
    REQUIRE( QDir().mkpath( screenshotDir ) );

    // The save-file dialog is native by default and cannot be driven offscreen
    QApplication::setAttribute( Qt::AA_DontUseNativeDialogs, true );

    QTemporaryFile file{ QDir::temp().filePath( "klogg_fx3_save_XXXXXX.log" ) };
    writeLines( file, mainLogLines() );

    MainWindowFixture fixture;
    REQUIRE( fixture.openFile( file.fileName(), kMainLogLines ) );

    fixture.crawler.runSearchAndWait( QStringLiteral( "ERROR" ) );
    REQUIRE(
        waitUiState( [ & ] { return fixture.crawler.filteredLines().get() == kExpectedErrors; } ) );

    const QString saveName = QStringLiteral( "klogg_fx3_filtered_%1.log" )
                                 .arg( QUuid::createUuid().toString( QUuid::WithoutBraces ) );
    const QString savePath = QDir::temp().filePath( saveName );
    armSaveDialogAnswer( QDir::temp().path(), saveName );

    bool savePicked = false;
    whenPopupMenu( [ &savePicked ]( QMenu* popup ) {
        if ( auto* action
             = findActionByText( popup->actions(), QStringLiteral( "Save to file" ) ) ) {
            action->trigger();
            savePicked = true;
            popup->close();
        }
    } );

    // Right-click the first filtered row; the save runs synchronously from there
    const auto charHeight = fixture.crawler.mainView()->fontMetrics().height();
    QTest::mouseClick(
        fixture.crawler.filteredViewViewport(), Qt::RightButton, Qt::NoModifier,
        QPoint( fixture.crawler.filteredViewViewport()->width() / 2, charHeight / 2 ) );
    REQUIRE( savePicked );
    REQUIRE( waitUiState( [ & ] { return QFileInfo::exists( savePath ); } ) );

    QFile saved{ savePath };
    REQUIRE( saved.open( QIODevice::ReadOnly ) );
    const auto savedLines
        = QString::fromUtf8( saved.readAll() ).split( QLatin1Char( '\n' ), Qt::SkipEmptyParts );
    REQUIRE( savedLines.size() == kExpectedErrors );
    REQUIRE( savedLines.first().contains( QLatin1String( "ERROR" ) ) );
    REQUIRE( savedLines.first().contains( QLatin1String( "requestId=6" ) ) );

    REQUIRE( saveShot( fixture.window.get(), screenshotDir, "48_save_filtered.png" ) );

    QFile::remove( savePath );
    QApplication::setAttribute( Qt::AA_DontUseNativeDialogs, false );
}
