/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

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

// This file implements MainWindow. It is responsible for creating and
// managing the menus, the toolbar, and the CrawlerWidget. It also
// load/save the settings on opening/closing of the app

#include "configuration.h"
#include "containers.h"
#include "log.h"
#include "log_qt.h"
#include <QNetworkReply>
#include <cassert>
#include <exception>

#include <iterator>
#include <algorithm>
#include <qaction.h>
#include <qapplication.h>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif // Q_OS_WIN

#include <QAtomicInt>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListView>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QProgressDialog>
#include <QResource>
#include <QScreen>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringListModel>
#include <QTemporaryFile>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QToolBar>
#include <QToolTip>
#include <QUrl>
#include <QUrlQuery>
#include <QWindow>
#include <QtConcurrent>

#include "mainwindow.h"

#include "apievents.h"
#include "apisession.h"
#include "clipboard.h"
#include "crawlerwidget.h"
#include "decompressor.h"
#include "dispatch_to.h"
#include "downloader.h"
#include "encodingmenu.h"
#include "eventconfigdialog.h"
#include "eventvisualization.h"
#include "favoritefiles.h"
#include "highlightercache.h"
#include "highlightersdialog.h"
#include "highlightersmenu.h"
#include "issuereporter.h"
#include "klogg/api.h"
#include "klogg_version.h"
#include "logger.h"
#include "logparsingrulesdialog.h"
#include "mainwindowtext.h"
#include "openfilehelper.h"
#include "optionsdialog.h"
#include "predefinedfiltersdialog.h"
#include "progress.h"
#include "readablesize.h"
#include "sessioninfo.h"
#include "shortcuts.h"
#include "sliceoverlaywidget.h"
#include "styles.h"
#include "tabbedcrawlerwidget.h"
#include "tempdir.h"
#include "uitracing.h"
#include "uiuserdata.h"

namespace {

void signalCrawlerToFollowFile( CrawlerWidget* crawler_widget )
{
    dispatchToMainThread( [ crawler_widget ]() { crawler_widget->followSet( true ); } );
}

static constexpr auto ClipboardMaxTry = 5;

} // namespace

QTranslator MainWindow::mTranslator;
QTranslator MainWindow::mQtTranslator;

MainWindow::MainWindow( WindowSession session )
    : session_( std::move( session ) )
    , mainIcon_()
    , iconLoader_( this )
    , signalMux_()
    , quickFindMux_( session_.getQuickFindPattern() )
    , mainTabWidget_()
    , recorderManager_( this )
{
    createActions();
    createMenus();
    createToolBars();

    setAcceptDrops( true );

    // Default geometry
    const auto* screen = QApplication::primaryScreen();
    if ( screen ) {
        const QRect geometry = screen->availableGeometry();
        setGeometry( geometry.x() + 20, geometry.y() + 40, geometry.width() - 140,
                     geometry.height() - 140 );
    }

    mainIcon_.addFile( ":/images/hicolor/16x16/klogg.png" );
    // mainIcon_.addFile( ":/images/hicolor/24x24/klogg.png" );
    mainIcon_.addFile( ":/images/hicolor/32x32/klogg.png" );
    mainIcon_.addFile( ":/images/hicolor/48x48/klogg.png" );

    setWindowIcon( mainIcon_ );
    readSettings();

    // Log type configs come from the Core API session (loaded at construction)
    updateEventCategoryMenu();

    createTrayIcon();

    // Connect the signals to the mux (they will be forwarded to the
    // "current" crawlerwidget

    // Send actions to the crawlerwidget
    signalMux_.connect( this, SIGNAL( followSet( bool ) ), SIGNAL( followSet( bool ) ) );
    signalMux_.connect( this, SIGNAL( textWrapSet( bool ) ), SIGNAL( textWrapSet( bool ) ) );
    signalMux_.connect( this, SIGNAL( chipModeSet( bool ) ), SIGNAL( chipModeSet( bool ) ) );
    signalMux_.connect( this, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );
    signalMux_.connect( this, SIGNAL( enteringQuickFind() ), SLOT( enteringQuickFind() ) );
    signalMux_.connect( &quickFindWidget_, SIGNAL( close() ), SLOT( exitingQuickFind() ) );

    // Actions from the CrawlerWidget
    signalMux_.connect( SIGNAL( followModeChanged( bool ) ), this,
                        SLOT( changeFollowMode( bool ) ) );
    signalMux_.connect(
        SIGNAL( newSelection( LineNumber, LinesCount, LineColumn, LineLength ) ), this,
        SLOT( lineNumberHandler( LineNumber, LinesCount, LineColumn, LineLength ) ) );
    signalMux_.connect( SIGNAL( saveCurrentSearchAsPredefinedFilter( QString, bool ) ), this,
                        SLOT( newPredefinedFilterHandler( QString, bool ) ) );

    signalMux_.connect( SIGNAL( sendToScratchpad( QString ) ), this,
                        SLOT( sendToScratchpad( QString ) ) );

    signalMux_.connect( SIGNAL( replaceDataInScratchpad( QString ) ), this,
                        SLOT( replaceDataInScratchpad( QString ) ) );

    // Register for progress status bar
    signalMux_.connect( SIGNAL( loadingProgressed( int ) ), this,
                        SLOT( updateLoadingProgress( int ) ) );
    signalMux_.connect( SIGNAL( loadingFinished( LoadingStatus ) ), this,
                        SLOT( handleLoadingFinished( LoadingStatus ) ) );

    signalMux_.connect( SIGNAL( filteredViewChanged() ), this,
                        SLOT( handleFilteredViewChanged() ) );
    
    // Connect event visibility request signal
    signalMux_.connect( SIGNAL( eventVisibilityRequested() ), this,
                        SLOT( onEventVisibilityRequested() ) );

    // Connect visibility changed signal (for category button state)
    signalMux_.connect( SIGNAL( visibilityChanged(bool) ), this,
                        SLOT( onVisibilityChanged(bool) ) );

    // Connect event rules changed signal
    signalMux_.connect( SIGNAL( eventRulesChanged() ), this,
                        SLOT( updateEventCategoryMenu() ) );

    signalMux_.connect( SIGNAL( selectedSourceLineChanged(LineNumber) ), this,
                        SLOT( onSelectedSourceLineChanged(LineNumber) ) );

    // Configure the main tabbed widget
    mainTabWidget_.setDocumentMode( true );
    mainTabWidget_.setMovable( true );
    // mainTabWidget_.setTabShape( QTabWidget::Triangular );
    mainTabWidget_.setTabsClosable( true );

    scratchPad_.setWindowIcon( mainIcon_ );
    scratchPad_.setWindowTitle( tr( "klogg - scratchpad" ) );

    connect( &mainTabWidget_, &TabbedCrawlerWidget::tabCloseRequested, this,
             [ this ]( int index ) { this->closeTab( index, ActionInitiator::User ); } );
    connect( &mainTabWidget_, &TabbedCrawlerWidget::openInNewWindowRequested, this,
             [ this ]( int /*tab*/ ) { openCurrentFileInNewWindow(); } );
    connect( &mainTabWidget_, &TabbedCrawlerWidget::currentChanged, this,
             &MainWindow::currentTabChanged );

    // Establish the QuickFindWidget and mux ( to send requests from the
    // QFWidget to the right window )
    connect( &quickFindWidget_, SIGNAL( patternConfirmed( const QString&, bool, bool ) ),
             &quickFindMux_, SLOT( confirmPattern( const QString&, bool, bool ) ) );
    connect( &quickFindWidget_, SIGNAL( patternUpdated( const QString&, bool, bool ) ),
             &quickFindMux_, SLOT( setNewPattern( const QString&, bool, bool ) ) );
    connect( &quickFindWidget_, SIGNAL( cancelSearch() ), &quickFindMux_, SLOT( cancelSearch() ) );
    connect( &quickFindWidget_, SIGNAL( searchForward() ), &quickFindMux_,
             SLOT( searchForward() ) );
    connect( &quickFindWidget_, SIGNAL( searchBackward() ), &quickFindMux_,
             SLOT( searchBackward() ) );
    connect( &quickFindWidget_, SIGNAL( searchNext() ), &quickFindMux_, SLOT( searchNext() ) );

    // QuickFind changes coming from the views
    connect( &quickFindMux_, SIGNAL( patternChanged( const QString& ) ), this,
             SLOT( changeQFPattern( const QString& ) ) );
    connect( &quickFindMux_, SIGNAL( notify( const QFNotification& ) ), &quickFindWidget_,
             SLOT( notify( const QFNotification& ) ) );
    connect( &quickFindMux_, SIGNAL( clearNotification() ), &quickFindWidget_,
             SLOT( clearNotification() ) );

    // Construct the QuickFind bar
    quickFindWidget_.hide();

    QWidget* central_widget = new QWidget();
    auto* main_layout = new QVBoxLayout();
    main_layout->setContentsMargins( 0, 0, 0, 0 );
    main_layout->addWidget( &mainTabWidget_ );
    main_layout->addWidget( &quickFindWidget_ );
    central_widget->setLayout( main_layout );

    setCentralWidget( central_widget );

    sliceOverlayWidget_ = new SliceOverlayWidget( this );
    sliceOverlayWidget_->hide();

    sliceOverlayEnabled_ = PersistentInfo::getStore( app_settings{} )
                               .getBool( "eventVisualization/sliceHoverOverlayEnabled", false );

    updateTitleBar( "" );
    loadIcons();
    reTranslateUI();

    connect( &recorderManager_, &RecorderManager::recordingStarted, this,
             [ this ]() { updateRecordActionState(); } );
    connect( &recorderManager_, &RecorderManager::recordingStopped, this,
             [ this ]() {
                 recordingFileName_.clear();
                 updateRecordActionState();
             } );
    connect( &recorderManager_, &RecorderManager::recordingError, this,
             [ this ]( const QString& error ) {
                 QMessageBox::warning( this, tr( "klogg" ),
                                       tr( "Recorder process failed: %1" ).arg( error ) );
             } );
    connect( this, &MainWindow::optionsChanged, this, [ this ]() {
        updateRecordActionState();
    } );
    updateSliceOverlayPosition();
}

void MainWindow::reloadGeometry()
{
    QByteArray geometry;

    session_.restoreGeometry( &geometry );
    restoreGeometry( geometry );
}

void MainWindow::reloadSession()
{
    const auto& config = Configuration::get();
    const auto followFileOnLoad = config.followFileOnLoad() && config.anyFileWatchEnabled();

    int current_file_index = -1;
    const auto openedFiles
        = session_.restore( [] { return new CrawlerWidget(); }, &current_file_index );

    for ( const auto& open_file : openedFiles ) {
        QString file_name = { open_file.first };
        auto* crawler_widget = static_cast<CrawlerWidget*>( open_file.second );

        if ( crawler_widget ) {
            mainTabWidget_.addCrawler( crawler_widget, file_name );

            if ( followFileOnLoad ) {
                signalCrawlerToFollowFile( crawler_widget );
            }
        }
    }

    const auto bindings = Configuration::get().recorderFileBindings();
    for ( auto it = bindings.begin(); it != bindings.end(); ++it ) {
        recorderBindings_[ QDir::cleanPath( it.key() ) ] = it.value();
    }

    if ( current_file_index >= 0 ) {
        mainTabWidget_.setCurrentIndex( current_file_index );

        if ( followFileOnLoad ) {
            followAction->setChecked( true );
        }
    }

    updateOpenedFilesMenu();
}

void MainWindow::loadInitialFile( QString fileName, bool followFile )
{
    LOG_DEBUG << "loadInitialFile";

    // Is there a file passed as argument?
    if ( !fileName.isEmpty() ) {
        loadFile( fileName, followFile );
    }
}

void MainWindow::reTranslateUI()
{
    using namespace klogg::mainwindow;
    // menu
    auto transMenu = []( const char* text ) -> auto {
        return QApplication::translate( "klogg::mainwindow::menu", text );
    };
    fileMenu->setTitle( transMenu( menu::fileTitle ) );
    editMenu->setTitle( transMenu( menu::editTitle ) );
    viewMenu->setTitle( transMenu( menu::viewTitle ) );
    openedFilesMenu->setTitle( transMenu( menu::openedFilesTitle ) );
    toolsMenu->setTitle( transMenu( menu::toolsTitle ) );
    highlightersMenu->setTitle( transMenu( menu::highlightersTitle ) );
    favoritesMenu->setTitle( transMenu( menu::favoritesTitle ) );
    helpMenu->setTitle( transMenu( menu::helpTitle ) );

    // toolbar
    toolBar->setToolTip(
        QApplication::translate( "klogg::mainwindow::toolbar", toolbar::toolbarTitle ) );

    // action
    auto transAction = []( const char* text ) -> auto {
        return QApplication::translate( "klogg::mainwindow::action", text );
    };
    newWindowAction->setText( transAction( action::newWindowText ) );
    newWindowAction->setStatusTip( transAction( action::newWindowStatusTip ) );

    openInNewWindowAction->setText( transAction( action::openInNewWindowText ) );
    openInNewWindowAction->setStatusTip( transAction( action::openInNewWindowStatusTip ) );

    openAction->setText( transAction( action::openText ) );
    openAction->setStatusTip( transAction( action::openStatusTip ) );

    recentFilesCleanup->setText( transAction( action::recentFilesCleanupText ) );

    closeAction->setText( transAction( action::closeText ) );
    closeAction->setStatusTip( transAction( action::closeStatusTip ) );

    closeAllAction->setText( transAction( action::closeAllText ) );
    closeAllAction->setStatusTip( transAction( action::closeAllStatusTip ) );

    exitAction->setText( transAction( action::exitText ) );
    exitAction->setStatusTip( transAction( action::exitStatusTip ) );

    copyAction->setText( transAction( action::copyText ) );
    copyAction->setStatusTip( transAction( action::copyStatusTip ) );

    selectAllAction->setText( transAction( action::selectAllText ) );
    selectAllAction->setStatusTip( transAction( action::selectAllStatusTip ) );

    goToLineAction->setText( transAction( action::goToLineText ) );
    goToLineAction->setStatusTip( transAction( action::goToLineStatusTip ) );

    findAction->setText( transAction( action::findText ) );
    findAction->setStatusTip( transAction( action::findStatusTip ) );

    clearLogAction->setText( transAction( action::clearLogText ) );
    clearLogAction->setStatusTip( transAction( action::clearLogStatusTip ) );

    openContainingFolderAction->setText( transAction( action::openContainingFolderText ) );
    openContainingFolderAction->setStatusTip(
        transAction( action::openContainingFolderStatusTip ) );

    openInEditorAction->setText( transAction( action::openInEditorText ) );
    openInEditorAction->setStatusTip( transAction( action::openInEditorStatusTip ) );

    copyPathToClipboardAction->setText( transAction( action::copyPathToClipboardText ) );
    copyPathToClipboardAction->setStatusTip( transAction( action::copyPathToClipboardStatusTip ) );

    openClipboardAction->setText( transAction( action::openClipboardText ) );
    openClipboardAction->setStatusTip( transAction( action::openClipboardStatusTip ) );

    openUrlAction->setText( transAction( action::openUrlText ) );
    openUrlAction->setStatusTip( transAction( action::openUrlStatusTip ) );

    overviewVisibleAction->setText( transAction( action::overviewVisibleText ) );

    lineNumbersVisibleInMainAction->setText( transAction( action::lineNumbersVisibleInMainText ) );
    lineNumbersVisibleInFilteredAction->setText(
        transAction( action::lineNumbersVisibleInFilteredText ) );

    followAction->setText( transAction( action::followText ) );
    goToTopAction->setText( transAction( action::goToTopText ) );
    textWrapAction->setText( transAction( action::wrapText ) );
    reloadAction->setText( transAction( action::reloadText ) );
    stopAction->setText( transAction( action::stopText ) );

    recordAction->setText( transAction( action::recordText ) );
    recordAction->setStatusTip( transAction( action::recordStatusTip ) );

    optionsAction->setText( transAction( action::optionsText ) );
    optionsAction->setStatusTip( transAction( action::optionsStatusTip ) );

    editHighlightersAction->setText( transAction( action::editHighlightersText ) );
    editHighlightersAction->setStatusTip( transAction( action::editHighlightersStatusTip ) );

    showDocumentationAction->setText( transAction( action::showDocumentationText ) );
    showDocumentationAction->setStatusTip( transAction( action::showDocumentationStatusTip ) );

    aboutAction->setText( transAction( action::aboutText ) );
    aboutAction->setStatusTip( transAction( action::aboutStatusTip ) );

    aboutQtAction->setText( transAction( action::aboutQtText ) );
    aboutQtAction->setStatusTip( transAction( action::aboutQtStatusTip ) );

    reportIssueAction->setText( transAction( action::reportIssueText ) );
    reportIssueAction->setStatusTip( transAction( action::reportIssueStatusTip ) );

    generateDumpAction->setText( transAction( action::generateDumpText ) );
    generateDumpAction->setStatusTip( transAction( action::generateDumpStatusTip ) );

    showScratchPadAction->setText( transAction( action::showScratchPadText ) );
    showScratchPadAction->setStatusTip( transAction( action::showScratchPadStatusTip ) );

    auto curFavoritesIconText = addToFavoritesAction->data().toBool()
                                    ? transAction( action::addToFavoritesText )
                                    : transAction( action::removeFromFavoritesText );
    addToFavoritesAction->setText( curFavoritesIconText );
    addToFavoritesMenuAction->setText( transAction( action::addToFavoritesText ) );

    removeFromFavoritesAction->setText( transAction( action::removeFromFavoritesText ) );

    selectOpenFileAction->setText( transAction( action::selectOpenFileText ) );

    predefinedFiltersDialogAction->setText( transAction( action::predefinedFiltersDialogText ) );
    predefinedFiltersDialogAction->setStatusTip(
        transAction( action::predefinedFiltersDialogStatusTip ) );

    visualizeInKloggAction->setText( tr( "Visualize in klogg" ) );
    visualizeInKloggAction->setStatusTip( tr( "Show slice visualization overlay inside klogg" ) );

    exportEventsAction->setText( tr( "Export Events" ) );
    exportEventsAction->setStatusTip( tr( "Export events to Perfetto JSON format" ) );

    exportToPerfettoUIAction->setText( tr( "Visualize in Perfetto" ) );
    exportToPerfettoUIAction->setStatusTip(
        tr( "Export events and open in Perfetto UI for visualization" ) );

    manageEventRulesAction->setText( tr( "Manage Event Rules" ) );
    manageEventRulesAction->setStatusTip( tr( "Manage event rules and categories" ) );

    logParsingRulesAction->setText( tr( "Log Parsing Rules" ) );
    logParsingRulesAction->setStatusTip(
        tr( "Configure log parsing rules to extract timestamp, PID, etc." ) );

    // trayIcon
    trayIcon_->setToolTip( QApplication::translate( "klogg::mainwindow::trayicon",
                                                    klogg::mainwindow::trayicon::trayiconTip ) );
}

int MainWindow::installLanguage( QString lang )
{
    if ( lang.isEmpty() ) {
        return -1;
    }

    QApplication::removeTranslator( &mTranslator );
    QApplication::removeTranslator( &mQtTranslator );

    QString qtPath( ":/i18n/qt_" + lang + ".qm" );
    QResource qtTranslations( qtPath );
    if ( !mQtTranslator.load( qtTranslations.data(), (int)qtTranslations.size() ) ) {
        LOG_ERROR << "load fail";
        return -1;
    }
    if ( !QApplication::installTranslator( &mQtTranslator ) ) {
        LOG_ERROR << "install fail";
        return -1;
    }

    QString appPath( ":/i18n/" + lang + ".qm" );
    QResource appTranslations( appPath );
    if ( !mTranslator.load( appTranslations.data(), (int)appTranslations.size() ) ) {
        LOG_ERROR << "load fail";
        return -1;
    }
    if ( !QApplication::installTranslator( &mTranslator ) ) {
        LOG_ERROR << "install fail";
        return -1;
    }

    return 0;
}

// Menu actions
void MainWindow::createActions()
{
    const auto& config = Configuration::get();
    const auto shortcuts = config.shortcuts();

    using namespace klogg::mainwindow;

    newWindowAction = new QAction( tr( action::newWindowText ), this );
    newWindowAction->setStatusTip( tr( action::newWindowStatusTip ) );
    connect( newWindowAction, &QAction::triggered, [ =, this ] { Q_EMIT newWindow(); } );
    newWindowAction->setVisible( config.allowMultipleWindows() );

    openInNewWindowAction = new QAction( tr( action::openInNewWindowText ), this );
    openInNewWindowAction->setStatusTip( tr( action::openInNewWindowStatusTip ) );
    connect( openInNewWindowAction, &QAction::triggered,
             [ this ] { openCurrentFileInNewWindow(); } );

    openAction = new QAction( tr( action::openText ), this );
    openAction->setStatusTip( tr( action::openStatusTip ) );
    connect( openAction, &QAction::triggered, [ this ]( auto ) { this->open(); } );

    recentFilesCleanup = new QAction( tr( action::recentFilesCleanupText ), this );
    connect( recentFilesCleanup, &QAction::triggered, this,
             [ this ]( auto ) { this->clearRecentFileActions(); } );

    closeAction = new QAction( tr( action::closeText ), this );
    closeAction->setStatusTip( tr( action::closeStatusTip ) );
    connect( closeAction, &QAction::triggered, this,
             [ this ]( auto ) { this->closeTab( ActionInitiator::User ); } );

    closeAllAction = new QAction( tr( action::closeAllText ), this );
    closeAllAction->setStatusTip( tr( action::closeAllStatusTip ) );
    connect( closeAllAction, &QAction::triggered, this,
             [ this ]( auto ) { this->closeAll( ActionInitiator::User ); } );

    recentFilesGroup = new QActionGroup( this );
    connect( recentFilesGroup, &QActionGroup::triggered, this, &MainWindow::openFileFromRecent );
    for ( auto i = 0u; i < recentFileActions.size(); ++i ) {
        recentFileActions[ i ] = new QAction( this );
        connect( recentFileActions[ i ], &QAction::hovered, [ this, a = recentFileActions[ i ] ]() {
            QToolTip::showText( QCursor::pos(), a->toolTip(), this );
        } );
        recentFileActions[ i ]->setVisible( false );
        recentFileActions[ i ]->setActionGroup( recentFilesGroup );
    }

    exitAction = new QAction( tr( action::exitText ), this );
    exitAction->setStatusTip( tr( action::exitStatusTip ) );
    connect( exitAction, &QAction::triggered, this, &MainWindow::exitRequested );

    copyAction = new QAction( tr( action::copyText ), this );
    copyAction->setStatusTip( tr( action::copyStatusTip ) );
    connect( copyAction, &QAction::triggered, this, [ this ]( auto ) { this->copy(); } );

    selectAllAction = new QAction( tr( action::selectAllText ), this );
    selectAllAction->setStatusTip( tr( action::selectAllStatusTip ) );
    connect( selectAllAction, &QAction::triggered, this, [ this ]( auto ) { this->selectAll(); } );

    goToLineAction = new QAction( tr( action::goToLineText ), this );
    goToLineAction->setStatusTip( tr( action::goToLineStatusTip ) );
    signalMux_.connect( goToLineAction, SIGNAL( triggered() ), SLOT( goToLine() ) );

    findAction = new QAction( tr( action::findText ), this );
    findAction->setStatusTip( tr( action::findStatusTip ) );
    connect( findAction, &QAction::triggered, this, [ this ]( auto ) { this->find(); } );

    clearLogAction = new QAction( tr( action::clearLogText ), this );
    clearLogAction->setStatusTip( tr( action::clearLogStatusTip ) );
    connect( clearLogAction, &QAction::triggered, this, [ this ]( auto ) { this->clearLog(); } );

    openContainingFolderAction = new QAction( tr( action::openContainingFolderText ), this );
    openContainingFolderAction->setStatusTip( tr( action::openContainingFolderStatusTip ) );
    connect( openContainingFolderAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openContainingFolder(); } );

    openInEditorAction = new QAction( tr( action::openInEditorText ), this );
    openInEditorAction->setStatusTip( tr( action::openInEditorStatusTip ) );
    connect( openInEditorAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openInEditor(); } );

    copyPathToClipboardAction = new QAction( tr( action::copyPathToClipboardText ), this );
    copyPathToClipboardAction->setStatusTip( tr( action::copyPathToClipboardStatusTip ) );
    connect( copyPathToClipboardAction, &QAction::triggered, this,
             [ this ]( auto ) { this->copyFullPath(); } );

    copyWithColorAction = new QAction( tr( action::copyWithColorText ), this );
    copyWithColorAction->setStatusTip( tr( action::copyWithColorStatusTip ) );
    connect( copyWithColorAction, &QAction::triggered, this,
             [ this ]( auto ) { this->copyWithColor(); } );

    openClipboardAction = new QAction( tr( action::openClipboardText ), this );
    openClipboardAction->setStatusTip( tr( action::openClipboardStatusTip ) );
    connect( openClipboardAction, &QAction::triggered, this,
             [ this ]( auto ) { this->openClipboard(); } );

    openUrlAction = new QAction( tr( action::openUrlText ), this );
    openUrlAction->setStatusTip( tr( action::openUrlStatusTip ) );
    connect( openUrlAction, &QAction::triggered, this, [ this ]( auto ) { this->openUrl(); } );

    overviewVisibleAction = new QAction( tr( action::overviewVisibleText ), this );
    overviewVisibleAction->setCheckable( true );
    overviewVisibleAction->setChecked( config.isOverviewVisible() );
    connect( overviewVisibleAction, &QAction::toggled, this,
             &MainWindow::toggleOverviewVisibility );

    lineNumbersVisibleInMainAction
        = new QAction( tr( action::lineNumbersVisibleInMainText ), this );
    lineNumbersVisibleInMainAction->setCheckable( true );
    lineNumbersVisibleInMainAction->setChecked( config.mainLineNumbersVisible() );
    connect( lineNumbersVisibleInMainAction, &QAction::toggled, this,
             &MainWindow::toggleMainLineNumbersVisibility );

    lineNumbersVisibleInFilteredAction
        = new QAction( tr( action::lineNumbersVisibleInFilteredText ), this );
    lineNumbersVisibleInFilteredAction->setCheckable( true );
    lineNumbersVisibleInFilteredAction->setChecked( config.filteredLineNumbersVisible() );
    connect( lineNumbersVisibleInFilteredAction, &QAction::toggled, this,
             &MainWindow::toggleFilteredLineNumbersVisibility );

    followAction = new QAction( tr( action::followText ), this );
    followAction->setCheckable( true );
    followAction->setEnabled( config.anyFileWatchEnabled() );
    connect( followAction, &QAction::toggled, this, &MainWindow::followSet );

    goToTopAction = new QAction( tr( action::goToTopText ), this );
    signalMux_.connect( goToTopAction, SIGNAL( triggered() ), SLOT( jumpToTop() ) );

    textWrapAction = new QAction( tr( action::wrapText ), this );
    textWrapAction->setCheckable( true );
    textWrapAction->setEnabled( true );
    connect( textWrapAction, &QAction::toggled, this, &MainWindow::textWrapSet );

    chipModeAction = new QAction( tr( "Chip Input Mode" ), this );
    chipModeAction->setCheckable( true );
    chipModeAction->setStatusTip( tr( "Toggle chip input mode for search patterns" ) );
    globalChipMode_ = Configuration::get().chipMode();
    chipModeAction->setChecked( globalChipMode_ );
    connect( chipModeAction, &QAction::toggled, this, [ this ]( bool checked ) {
        globalChipMode_ = checked;
        Configuration::get().setChipMode( checked );
        Configuration::get().save();
        // Apply to all crawlers since chip mode is global
        const int count = mainTabWidget_.count();
        for ( int i = 0; i < count; ++i ) {
            auto* crawler = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( i ) );
            if ( crawler ) {
                crawler->setChipMode( checked );
            }
        }
    } );

    reloadAction = new QAction( tr( action::reloadText ), this );
    signalMux_.connect( reloadAction, SIGNAL( triggered() ), SLOT( reload() ) );

    stopAction = new QAction( tr( action::stopText ), this );
    stopAction->setEnabled( true );
    signalMux_.connect( stopAction, SIGNAL( triggered() ), SLOT( stopLoading() ) );

    optionsAction = new QAction( tr( action::optionsText ), this );
    optionsAction->setMenuRole( QAction::PreferencesRole );
    optionsAction->setStatusTip( tr( action::optionsStatusTip ) );
    connect( optionsAction, &QAction::triggered, this, [ this ]( auto ) { this->options(); } );

    editHighlightersAction = new QAction( tr( action::editHighlightersText ), this );
    editHighlightersAction->setMenuRole( QAction::NoRole );
    editHighlightersAction->setStatusTip( tr( action::editHighlightersStatusTip ) );
    connect( editHighlightersAction, &QAction::triggered, this,
             [ this ]( auto ) { this->editHighlighters(); } );

    showDocumentationAction = new QAction( tr( action::showDocumentationText ), this );
    showDocumentationAction->setStatusTip( tr( action::showDocumentationStatusTip ) );
    connect( showDocumentationAction, &QAction::triggered, this,
             [ this ]( auto ) { this->documentation(); } );

    aboutAction = new QAction( tr( action::aboutText ), this );
    aboutAction->setStatusTip( tr( action::aboutStatusTip ) );
    connect( aboutAction, &QAction::triggered, this, [ this ]( auto ) { this->about(); } );

    aboutQtAction = new QAction( tr( action::aboutQtText ), this );
    aboutQtAction->setStatusTip( tr( action::aboutQtStatusTip ) );
    connect( aboutQtAction, &QAction::triggered, this, [ this ]( auto ) { this->aboutQt(); } );

    reportIssueAction = new QAction( tr( action::reportIssueText ), this );
    reportIssueAction->setStatusTip( tr( action::reportIssueStatusTip ) );
    connect( reportIssueAction, &QAction::triggered, this,
             []( auto ) { IssueReporter::reportIssue( IssueTemplate::Bug ); } );

    generateDumpAction = new QAction( tr( action::generateDumpText ), this );
    generateDumpAction->setStatusTip( tr( action::generateDumpStatusTip ) );
    connect( generateDumpAction, &QAction::triggered, this,
             [ this ]( auto ) { this->generateDump(); } );

    showScratchPadAction = new QAction( tr( action::showScratchPadText ), this );
    showScratchPadAction->setStatusTip( tr( action::showScratchPadStatusTip ) );
    connect( showScratchPadAction, &QAction::triggered, this,
             [ this ]( auto ) { this->showScratchPad(); } );

    encodingGroup = new QActionGroup( this );
    connect( encodingGroup, &QActionGroup::triggered, this, &MainWindow::encodingChanged );

    favoritesGroup = new QActionGroup( this );
    connect( favoritesGroup, &QActionGroup::triggered, this, &MainWindow::openFileFromFavorites );

    openedFilesGroup = new QActionGroup( this );
    connect( openedFilesGroup, &QActionGroup::triggered, this, &MainWindow::switchToOpenedFile );

    addToFavoritesAction = new QAction( tr( action::addToFavoritesText ), this );
    addToFavoritesAction->setData( true );
    connect( addToFavoritesAction, &QAction::triggered, this,
             [ this ]( auto ) { this->addToFavorites(); } );

    addToFavoritesMenuAction = new QAction( tr( action::addToFavoritesText ), this );
    connect( addToFavoritesMenuAction, &QAction::triggered, this,
             [ this ]( auto ) { this->addToFavorites(); } );

    removeFromFavoritesAction = new QAction( tr( action::removeFromFavoritesText ), this );
    connect( removeFromFavoritesAction, &QAction::triggered, this,
             [ this ]( auto ) { this->removeFromFavorites(); } );

    selectOpenFileAction = new QAction( tr( action::selectOpenFileText ), this );
    connect( selectOpenFileAction, &QAction::triggered, this,
             [ this ]( auto ) { this->selectOpenedFile(); } );

    predefinedFiltersDialogAction = new QAction( tr( action::predefinedFiltersDialogText ), this );
    predefinedFiltersDialogAction->setStatusTip( tr( action::predefinedFiltersDialogStatusTip ) );
    connect( predefinedFiltersDialogAction, &QAction::triggered, this,
             [ this ]( auto ) { this->editPredefinedFilters(); } );

    recordAction = new QAction( tr( action::recordText ), this );
    recordAction->setCheckable( true );
    recordAction->setStatusTip( tr( action::recordStatusTip ) );
    connect( recordAction, &QAction::triggered, this, [ this ]( auto ) {
        auto crawler = currentCrawlerWidget();
        if ( !crawler ) {
            return;
        }
        if ( recorderManager_.isRecording() ) {
            recorderManager_.stop();
        }
        else if ( !recorderManager_.boundCommandName().isEmpty() ) {
            const auto fileName = QDir::cleanPath( session_.getFilename( crawler ) );
            const auto commands = Configuration::get().recorderCommands();
            const auto it = std::find_if( commands.begin(), commands.end(),
                                           [ this ]( const auto& cmd ) {
                                               return cmd.name
                                                      == recorderManager_.boundCommandName();
                                           } );
            if ( it != commands.end() ) {
                recordingFileName_ = fileName;
                recorderManager_.start( fileName, it->command );
            }
            else {
                showRecordCommandMenu();
            }
        }
        else {
            showRecordCommandMenu();
        }
    } );

    visualizeInKloggAction = new QAction( tr( "Visualize in klogg" ), this );
    visualizeInKloggAction->setCheckable( true );
    visualizeInKloggAction->setChecked( isSliceOverlayEnabled() );
    visualizeInKloggAction->setStatusTip(
        tr( "Show slice visualization overlay inside klogg" ) );
    connect( visualizeInKloggAction, &QAction::toggled, this,
             &MainWindow::setSliceOverlayEnabled );

    exportEventsAction = new QAction( tr( "Export Events" ), this );
    exportEventsAction->setStatusTip( tr( "Export events to Perfetto JSON format" ) );
    connect( exportEventsAction, &QAction::triggered, this,
             [ this ]( auto ) { this->exportEvents(); } );

    exportToPerfettoUIAction = new QAction( tr( "Visualize in Perfetto" ), this );
    exportToPerfettoUIAction->setStatusTip( tr( "Export events and open in Perfetto UI for visualization" ) );
    connect( exportToPerfettoUIAction, &QAction::triggered, this,
             [ this ]( auto ) { this->exportAndOpenInPerfettoUI(); } );

    manageEventRulesAction = new QAction( tr( "Manage Event Rules" ), this );
    manageEventRulesAction->setStatusTip( tr( "Manage event rules and categories" ) );
    connect( manageEventRulesAction, &QAction::triggered, this,
             [ this ]( auto ) { this->showEventRulesManager(); } );

    logParsingRulesAction = new QAction( tr( "Log Parsing Rules" ), this );
    logParsingRulesAction->setStatusTip( tr( "Configure log parsing rules to extract timestamp, PID, etc." ) );
    connect( logParsingRulesAction, &QAction::triggered, this,
             [ this ]( auto ) { this->showLogParsingRulesDialog(); } );

    updateShortcuts();
}

void MainWindow::updateShortcuts()
{
    const auto& config = Configuration::get();
    const auto shortcuts = config.shortcuts();

    for ( auto& shortcut : shortcuts_ ) {
        shortcut.second->deleteLater();
    }

    shortcuts_.clear();
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowOpenQfForward,
                                      [ this ] { displayQuickFindBar( QuickFindMux::Forward ); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowOpenQfBackward,
                                      [ this ] { displayQuickFindBar( QuickFindMux::Backward ); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowFocusSearchInput, [ this ] {
                                          if ( auto crawler = currentCrawlerWidget() ) {
                                              crawler->focusSearchEdit();
                                          }
                                      } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowFullScreen,
                                      [ this ] { this->showFullScreen(); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowMax,
                                      [ this ] { this->showMaximized(); } );
    ShortcutAction::registerShortcut( shortcuts, shortcuts_, this, Qt::WindowShortcut,
                                      ShortcutAction::MainWindowMin,
                                      [ this ] { this->showMinimized(); } );

    auto setShortcuts = [ &shortcuts ]( auto* action, const auto& actionName ) {
        action->setShortcuts( ShortcutAction::shortcutKeys( actionName, shortcuts ) );
    };

    setShortcuts( newWindowAction, ShortcutAction::MainWindowNewWindow );
    setShortcuts( openAction, ShortcutAction::MainWindowOpenFile );
    setShortcuts( closeAction, ShortcutAction::MainWindowCloseFile );
    setShortcuts( closeAllAction, ShortcutAction::MainWindowCloseAll );
    setShortcuts( exitAction, ShortcutAction::MainWindowQuit );
    setShortcuts( copyAction, ShortcutAction::MainWindowCopy );
    setShortcuts( selectAllAction, ShortcutAction::MainWindowSelectAll );
    setShortcuts( findAction, ShortcutAction::MainWindowOpenQf );
    setShortcuts( clearLogAction, ShortcutAction::MainWindowClearFile );
    setShortcuts( openContainingFolderAction, ShortcutAction::MainWindowOpenContainingFolder );
    setShortcuts( openInEditorAction, ShortcutAction::MainWindowOpenInEditor );
    setShortcuts( copyPathToClipboardAction, ShortcutAction::MainWindowCopyPathToClipboard );
    setShortcuts( copyWithColorAction, ShortcutAction::MainWindowCopyWithColor );
    setShortcuts( openClipboardAction, ShortcutAction::MainWindowOpenFromClipboard );
    setShortcuts( openUrlAction, ShortcutAction::MainWindowOpenFromUrl );
    setShortcuts( followAction, ShortcutAction::MainWindowFollowFile );
    setShortcuts( goToTopAction, ShortcutAction::MainWindowGoToTop );
    setShortcuts( textWrapAction, ShortcutAction::MainWindowTextWrap );
    setShortcuts( reloadAction, ShortcutAction::MainWindowReload );
    setShortcuts( stopAction, ShortcutAction::MainWindowStop );
    setShortcuts( showScratchPadAction, ShortcutAction::MainWindowScratchpad );
    setShortcuts( selectOpenFileAction, ShortcutAction::MainWindowSelectOpenFile );
    setShortcuts( goToLineAction, ShortcutAction::LogViewJumpToLine );
    setShortcuts( optionsAction, ShortcutAction::MainWindowPreference );
}

void MainWindow::loadIcons()
{
    openAction->setIcon( iconLoader_.load( "icons8-open-file" ) );
    stopAction->setIcon( iconLoader_.load( "icons8-delete" ) );
    reloadAction->setIcon( iconLoader_.load( "icons8-restore-page" ) );
    followAction->setIcon( iconLoader_.load( "icons8-fast-forward" ) );
    goToTopAction->setIcon( iconLoader_.load( "icons8-up" ) );
    showScratchPadAction->setIcon( iconLoader_.load( "icons8-create" ) );
    addToFavoritesAction->setIcon( iconLoader_.load( "icons8-star" ) );
    addToFavoritesMenuAction->setIcon( iconLoader_.load( "icons8-star" ) );
    recordAction->setIcon( iconLoader_.load( "icons8-plus" ) );

    const auto& config = Configuration::get();
    const auto palette = config.themePalette( config.theme() );
    auto checkedColor = palette.count( "ToggleCheckedText" ) > 0
                            ? QColor( palette.at( "ToggleCheckedText" ) )
                            : QColor();
    if ( !checkedColor.isValid() ) {
        checkedColor = palette.count( "HighlightedText" ) > 0
                           ? QColor( palette.at( "HighlightedText" ) )
                           : qApp->palette().color( QPalette::HighlightedText );
    }
    if ( !checkedColor.isValid() ) {
        checkedColor = QColor( "#ffffff" );
    }

    auto checkedBackground = palette.count( "ToggleCheckedBackground" ) > 0
                                 ? QColor( palette.at( "ToggleCheckedBackground" ) )
                                 : QColor();
    if ( !checkedBackground.isValid() ) {
        checkedBackground = palette.count( "Highlight" ) > 0
                                ? QColor( palette.at( "Highlight" ) )
                                : qApp->palette().color( QPalette::Highlight );
    }
    if ( !checkedBackground.isValid() ) {
        checkedBackground = QColor( "#3e4451" );
    }

    const auto toolbarCheckedStyle
        = QString( "QToolButton:checked { color: %1; background-color: %2; }" )
              .arg( checkedColor.name( QColor::HexArgb ) )
              .arg( checkedBackground.name( QColor::HexArgb ) );

    const auto toolbarButtons = toolBar->findChildren<QToolButton*>();
    for ( auto* button : toolbarButtons ) {
        if ( button != nullptr ) {
            button->setStyleSheet( toolbarCheckedStyle );
        }
    }
}

void MainWindow::createMenus()
{
    using namespace klogg::mainwindow;

    fileMenu = menuBar()->addMenu( tr( menu::fileTitle ) );
    fileMenu->setToolTipsVisible( true );
    fileMenu->addAction( newWindowAction );
    fileMenu->addAction( openInNewWindowAction );
    fileMenu->addAction( openAction );
    fileMenu->addAction( openClipboardAction );
    fileMenu->addAction( openUrlAction );
    recentFilesMenu = fileMenu->addMenu( tr( "Open Recent" ) );
    for ( auto i = 0u; i < recentFileActions.size(); ++i ) {
        recentFilesMenu->addAction( recentFileActions[ i ] );
    }
    recentFilesMenu->addSeparator();
    recentFilesMenu->addAction( recentFilesCleanup );
    recentFilesMenu->setEnabled( false );
    fileMenu->addSeparator();

    fileMenu->addAction( closeAction );
    fileMenu->addAction( closeAllAction );
    fileMenu->addSeparator();

    fileMenu->addAction( optionsAction );
    fileMenu->addSeparator();

    fileMenu->addSeparator();
    fileMenu->addAction( exitAction );

    editMenu = menuBar()->addMenu( tr( menu::editTitle ) );
    editMenu->addAction( copyAction );
    editMenu->addAction( copyWithColorAction );
    editMenu->addAction( selectAllAction );
    editMenu->addSeparator();
    editMenu->addAction( findAction );
    editMenu->addSeparator();
    editMenu->addAction( goToLineAction );
    editMenu->addSeparator();
    editMenu->addAction( copyPathToClipboardAction );
    editMenu->addAction( openContainingFolderAction );
    editMenu->addSeparator();
    editMenu->addAction( openInEditorAction );
    editMenu->addAction( clearLogAction );
    editMenu->setEnabled( false );

    viewMenu = menuBar()->addMenu( tr( menu::viewTitle ) );
    openedFilesMenu = viewMenu->addMenu( tr( menu::openedFilesTitle ) );
    viewMenu->addSeparator();
    viewMenu->addAction( overviewVisibleAction );
    viewMenu->addSeparator();
    viewMenu->addAction( lineNumbersVisibleInMainAction );
    viewMenu->addAction( lineNumbersVisibleInFilteredAction );
    viewMenu->addSeparator();
    viewMenu->addAction( textWrapAction );
    viewMenu->addAction( chipModeAction );
    viewMenu->addSeparator();
    viewMenu->addAction( followAction );
    viewMenu->addAction( goToTopAction );
    viewMenu->addSeparator();
    viewMenu->addAction( reloadAction );

    toolsMenu = menuBar()->addMenu( tr( menu::toolsTitle ) );

    highlightersMenu = new HighlightersMenu( tr( menu::highlightersTitle ), menuBar() );
    menuBar()->addMenu( highlightersMenu );
    highlightersMenu->setApplyChange( [ this ]() {
        auto crawler = currentCrawlerWidget();
        if ( crawler != nullptr ) {
            crawler->applyConfiguration();
        }
    } );

    toolsMenu->addAction( predefinedFiltersDialogAction );
    toolsMenu->addSeparator();
    toolsMenu->addAction( logParsingRulesAction );
    toolsMenu->addAction( visualizeInKloggAction );
    toolsMenu->addAction( exportToPerfettoUIAction );
    toolsMenu->addAction( exportEventsAction );
    toolsMenu->addAction( manageEventRulesAction );

    toolsMenu->addSeparator();
    toolsMenu->addAction( showScratchPadAction );

    menuBar()->addMenu( EncodingMenu::generate( encodingGroup ) );
    menuBar()->addSeparator();

    favoritesMenu = menuBar()->addMenu( tr( menu::favoritesTitle ) );
    favoritesMenu->setToolTipsVisible( true );

    helpMenu = menuBar()->addMenu( tr( menu::helpTitle ) );
    helpMenu->addAction( showDocumentationAction );
    helpMenu->addSeparator();
    helpMenu->addAction( reportIssueAction );
    helpMenu->addSeparator();
    helpMenu->addAction( generateDumpAction );
    helpMenu->addSeparator();
    helpMenu->addAction( aboutQtAction );
    helpMenu->addAction( aboutAction );
}

void MainWindow::createToolBars()
{
    infoLine = new PathLine();
    infoLine->setFrameStyle( QFrame::StyledPanel );
    infoLine->setFrameShadow( QFrame::Sunken );
    infoLine->setLineWidth( 0 );
    infoLine->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );

    sizeField = new QLabel();
    sizeField->setAlignment( Qt::AlignHCenter | Qt::AlignVCenter );

    dateField = new QLabel();
    dateField->setAlignment( Qt::AlignHCenter | Qt::AlignVCenter );

    encodingField = new QLabel();
    encodingField->setAlignment( Qt::AlignHCenter | Qt::AlignVCenter );

    lineNbField = new QLabel();
    lineNbField->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
    lineNbField->setContentsMargins( 2, 0, 2, 0 );

    // Event category button with dropdown menu for multi-select
    eventCategoryButton = new QToolButton();
    eventCategoryButton->setText(tr("Categories"));
    eventCategoryButton->setToolTip(tr("Filter events by category (multi-select)"));
    eventCategoryButton->setPopupMode(QToolButton::InstantPopup);
    eventCategoryButton->setAutoRaise(true);
    
    eventCategoryMenu = new QMenu(eventCategoryButton);
    eventCategoryMenu->installEventFilter(this);  // Keep menu open for checkable items
    eventCategoryAllAction = eventCategoryMenu->addAction(tr("All Categories"));
    eventCategoryAllAction->setCheckable(true);
    eventCategoryAllAction->setChecked(true);
    eventCategoryMenu->addSeparator();
    eventCategoryButton->setMenu(eventCategoryMenu);
    
    connect(eventCategoryMenu, &QMenu::triggered, this, &MainWindow::onEventCategoryChanged);

    toolBar = addToolBar( QApplication::translate( "klogg::mainwindow::toolbar",
                                                   klogg::mainwindow::toolbar::toolbarTitle ) );
    toolBar->setIconSize( QSize( 16, 16 ) );
    toolBar->setMovable( false );
    toolBar->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );
    toolBar->addAction( openAction );
    toolBar->addAction( reloadAction );
    toolBar->addAction( followAction );
    toolBar->addAction( goToTopAction );
    toolBar->addAction( addToFavoritesAction );
    toolBar->addAction( recordAction );
    toolBar->addWidget( infoLine );
    toolBar->addAction( stopAction );
    QTimer::singleShot( 0, this, [ this ]() {
        auto btn = qobject_cast<QToolButton*>( toolBar->widgetForAction( recordAction ) );
        if ( btn ) {
            btn->setContextMenuPolicy( Qt::CustomContextMenu );
            connect( btn, &QToolButton::customContextMenuRequested, this, [ this ]() {
                if ( recorderManager_.isRecording()
                     || recorderManager_.boundCommandName().isEmpty() ) {
                    return;
                }
                QMenu menu;
                auto unbindAction = menu.addAction(
                    tr( klogg::mainwindow::action::unbindRecorderText ) );
                connect( unbindAction, &QAction::triggered, this,
                         [ this ]() { unbindRecorder(); } );
                menu.exec( QCursor::pos() );
            } );
        }
    } );

    infoToolbarSeparators.reserve( 6 );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );
    toolBar->addWidget( sizeField );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );
    toolBar->addWidget( dateField );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );
    toolBar->addWidget( encodingField );
    infoToolbarSeparators.push_back( toolBar->addSeparator() );
    
    // Small spacing before line number
    auto* lineNbSpacer = new QWidget();
    lineNbSpacer->setFixedWidth(10);
    toolBar->addWidget( lineNbSpacer );
    
    toolBar->addWidget( lineNbField );
    
    // Fixed spacing before tools
    auto* fixedSpacer = new QWidget();
    fixedSpacer->setFixedWidth(20);
    toolBar->addWidget( fixedSpacer );
    
    // Tools on the right: Scratchpad and Event category
    toolBar->addAction( showScratchPadAction );
    toolBar->addWidget( eventCategoryButton );
    
    // Category button is disabled by default (visibility doesn't include Events initially)
    eventCategoryButton->setEnabled(false);

    showInfoLabels( false );
}

void MainWindow::createTrayIcon()
{
    trayIcon_ = new QSystemTrayIcon( this );

    QMenu* trayMenu = new QMenu( this );
    QAction* openWindowAction = new QAction( tr( "Open window" ), this );
    QAction* quitAction = new QAction( tr( "Quit" ), this );

    trayMenu->addAction( openWindowAction );
    trayMenu->addAction( quitAction );

    connect( openWindowAction, &QAction::triggered, this, &QMainWindow::show );
    connect( quitAction, &QAction::triggered, [ this ] {
        this->isCloseFromTray_ = true;
        this->close();
    } );

    trayIcon_->setIcon( mainIcon_ );
    trayIcon_->setToolTip( tr( klogg::mainwindow::trayicon::trayiconTip ) );
    trayIcon_->setContextMenu( trayMenu );

    connect( trayIcon_, &QSystemTrayIcon::activated,
             [ this ]( QSystemTrayIcon::ActivationReason reason ) {
                 switch ( reason ) {
                 case QSystemTrayIcon::Trigger:
                     if ( !this->isVisible() ) {
                         this->show();
                     }
                     else {
                         this->hide();
                     }
                     break;
                 default:
                     break;
                 }
             } );

    if ( Configuration::get().minimizeToTray() ) {
        trayIcon_->show();
    }
}
//
// Q_SLOTS:
//

// Opens the file selection dialog to select a new log file
void MainWindow::open()
{
    // Modal file dialog, so only the entry is marked; loadFile below is scoped
    KLOGG_UI_TRACE_INSTANT( "MainWindow::open" );
    QString defaultDir = ".";

    // Default to the path of the current file if there is one
    if ( auto current = currentCrawlerWidget() ) {
        QString current_file = session_.getFilename( current );
        QFileInfo fileInfo = QFileInfo( current_file );
        defaultDir = fileInfo.path();
    }

    const auto selectedFiles = QFileDialog::getOpenFileUrls(
        this, tr( "Open file" ), QUrl::fromLocalFile( defaultDir ), tr( "All files (*)" ) );

    std::vector<QUrl> localFiles;
    std::vector<QUrl> remoteFiles;

    std::partition_copy( selectedFiles.cbegin(), selectedFiles.cend(),
                         std::back_inserter( localFiles ), std::back_inserter( remoteFiles ),
                         []( const QUrl& url ) { return url.isLocalFile(); } );

    for ( const auto& localFile : localFiles ) {
        loadFile( localFile.toLocalFile() );
    }

    for ( const auto& remoteFile : remoteFiles ) {
        openRemoteFile( remoteFile );
    }
}

void MainWindow::openRemoteFile( const QUrl& url )
{
    // Download runs under a modal progress dialog; the scope measures the
    // whole user-visible download, not just GUI-thread work
    KLOGG_UI_TRACE_SCOPE( "MainWindow::openRemoteFile" );
    Downloader downloader;

    QProgressDialog progressDialog;
    progressDialog.setLabelText( tr( "Downloading %1" ).arg( url.toString() ) );

    connect( &downloader, &Downloader::downloadProgress,
             [ &progressDialog ]( qint64 bytesReceived, qint64 bytesTotal ) {
                 const auto progress = calculateProgress( bytesReceived, bytesTotal );
                 progressDialog.setRange( 0, 100 );
                 progressDialog.setValue( progress );
             } );

    connect( &downloader, &Downloader::finished,
             [ &progressDialog ]( bool isOk ) { progressDialog.done( isOk ? 0 : 1 ); } );

    auto tempFile = new QTemporaryFile(
        QDir( currentTempRoot() ).filePath( url.fileName() ), this );
    if ( tempFile->open() ) {
        downloader.download( url, tempFile );
        if ( !progressDialog.exec() ) {
            loadFile( tempFile->fileName() );
        }
        else {
            QMessageBox::critical( this, tr( "Klogg - File download" ), downloader.lastError() );
        }
    }
    else {
        QMessageBox::critical( this, tr( "Klogg - File download" ),
                               tr( "Failed to create temp file" ) );
    }
}

void MainWindow::switchToOpenedFile( QAction* action )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::switchToOpenedFile" );
    if ( !action ) {
        return;
    }

    loadFile( action->data().toString() );
}

void MainWindow::openFileFromRecent( QAction* action )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::openFileFromRecent" );
    if ( !action ) {
        return;
    }

    const auto filename = action->data().toString();
    if ( QFileInfo{ filename }.isReadable() ) {
        loadFile( filename );
    }
    else {
        const auto userAction = QMessageBox::question(
            this, tr( "klogg - remove from recent" ),
            tr( "Could not read file %1. Remove it from recent files?" ).arg( filename ),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No );

        if ( userAction == QMessageBox::Yes ) {
            removeFromRecent( filename );
        }
    }
}

void MainWindow::openFileFromFavorites( QAction* action )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::openFileFromFavorites" );
    if ( !action ) {
        return;
    }

    const auto filename = action->data().toString();
    if ( QFileInfo{ filename }.isReadable() ) {
        loadFile( filename );
    }
    else {
        const auto userAction = QMessageBox::question(
            this, tr( "klogg - remove from favorites" ),
            tr( "Could not read file %1. Remove it from favorites?" ).arg( filename ),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No );

        if ( userAction == QMessageBox::Yes ) {
            removeFromFavorites( filename );
        }
    }
}

// Close current tab
void MainWindow::closeTab( ActionInitiator initiator )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::closeTab" );
    int currentIndex = mainTabWidget_.currentIndex();

    if ( currentIndex >= 0 ) {
        closeTab( currentIndex, initiator );
    }
    else {
        this->close();
    }
}

// Close all tabs
void MainWindow::closeAll( ActionInitiator initiator )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::closeAll" );
    while ( mainTabWidget_.count() ) {
        closeTab( 0, initiator );
    }
}

// Select all the text in the currently selected view
void MainWindow::selectAll()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::selectAll" );
    if ( infoLine->hasFocus() ) {
        infoLine->setSelection( 0, klogg::isize( infoLine->text() ) );
    }
    else if ( auto current = currentCrawlerWidget(); current != nullptr ) {
        current->selectAll();
    }
}

// Copy the currently selected line into the clipboard
void MainWindow::copy()
{
    // getSelectedText walks the selection synchronously and may exceed a frame
    // on very large selections
    KLOGG_UI_TRACE_SCOPE( "MainWindow::copy" );
    try {
        if ( infoLine->hasFocus() && infoLine->hasSelectedText() ) {
            sendTextToClipboard( infoLine->selectedText() );
            return;
        }

        if ( auto current = currentCrawlerWidget(); current != nullptr ) {
            auto text = current->getSelectedText();
            text.replace( QChar::Null, QChar::Space );

            sendTextToClipboard( text, true );
        }
    } catch ( std::exception& err ) {
        LOG_ERROR << "failed to copy data to clipboard " << err.what();
    }
}

void MainWindow::copyWithColor()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::copyWithColor" );
    try {
        if ( auto current = currentCrawlerWidget(); current != nullptr ) {
            current->copyWithColor();
        }
    } catch ( std::exception& err ) {
        LOG_ERROR << "failed to copy data to clipboard " << err.what();
    }
}

// Display the QuickFind bar
void MainWindow::find()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::find" );
    displayQuickFindBar( QuickFindMux::Forward );
}

void MainWindow::clearLog()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::clearLog" );
    const auto current_file = session_.getFilename( currentCrawlerWidget() );
    if ( QMessageBox::warning(
             this, tr( "klogg - clear file" ),
             tr( "Clear file %1? File content will be removed from disk, this is irreversible" )
                 .arg( current_file ) )
         == QMessageBox::Yes ) {
        QFile::resize( current_file, 0 );
    }
}

void MainWindow::copyFullPath()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::copyFullPath" );
    const auto current_file = session_.getFilename( currentCrawlerWidget() );
    sendTextToClipboard( QDir::toNativeSeparators( current_file ) );
}

void MainWindow::openContainingFolder()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::openContainingFolder" );
    showPathInFileExplorer( session_.getFilename( currentCrawlerWidget() ) );
}

void MainWindow::openInEditor()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::openInEditor" );
    openFileInDefaultApplication( session_.getFilename( currentCrawlerWidget() ) );
}

QString MainWindow::currentTempRoot()
{
    return QString::fromStdString( klogg::resolveTempRoot( Configuration::get().tempDirectory().toStdString(),
                                                           QDir::temp().absolutePath().toStdString() ) );
}

void MainWindow::tryOpenClipboard( int tryTimes )
{
    auto clipboard = QGuiApplication::clipboard();
    auto text = clipboard->text();

    if ( text.isEmpty() && tryTimes > 0 ) {
        QTimer::singleShot( 50, [ tryTimes, this ]() { tryOpenClipboard( tryTimes - 1 ); } );
    }
    else {
        auto tempFile = new QTemporaryFile(
            QDir( currentTempRoot() ).filePath( "klogg_clipboard" ), this );
        if ( tempFile->open() ) {
            tempFile->write( text.toUtf8() );
            tempFile->flush();

            loadFile( tempFile->fileName() );
        }
    }
}

void MainWindow::openClipboard()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::openClipboard" );
    tryOpenClipboard( ClipboardMaxTry );
}

void MainWindow::openUrl()
{
    KLOGG_UI_TRACE_INSTANT( "MainWindow::openUrl" );
    bool ok;
    const auto urlInClipboard = QUrl::fromUserInput( QApplication::clipboard()->text() );
    const auto selectedUrl = urlInClipboard.isValid() ? urlInClipboard.toString() : QString{};

    QString url
        = QInputDialog::getText( this, tr( "Open URL as log file" ), tr( "URL to download:" ),
                                 QLineEdit::Normal, selectedUrl, &ok );
    if ( ok && !url.isEmpty() ) {
        openRemoteFile( url );
    }
}

// Opens the 'Highlighters' dialog box
void MainWindow::editHighlighters()
{
    // Modal dialog: mark the entry, the scope would only measure idle wait
    KLOGG_UI_TRACE_INSTANT( "MainWindow::editHighlighters" );
    HighlightersDialog dialog( this );
    signalMux_.connect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );

    connect( &dialog, &HighlightersDialog::optionsChanged,
             [ this ]() { updateHighlightersMenu(); } );

    dialog.exec();
    signalMux_.disconnect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );
}

// Opens dialog to configure predefined filters
void MainWindow::editPredefinedFilters( const QString& newFilter, bool useRegex )
{
    KLOGG_UI_TRACE_INSTANT( "MainWindow::editPredefinedFilters" );
    PredefinedFiltersDialog dialog( newFilter, useRegex, this );

    signalMux_.connect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );

    dialog.exec();
    signalMux_.disconnect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );
}

// Opens the 'Options' modal dialog box
void MainWindow::options()
{
    KLOGG_UI_TRACE_INSTANT( "MainWindow::options" );
    OptionsDialog dialog( this );
    signalMux_.connect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );

    connect( &dialog, &OptionsDialog::optionsChanged, [ this ]() {
        const auto& config = Configuration::get();
        logging::enableFileLogging( config.enableLogging(),
                                    static_cast<logging::LogLevel>( config.loggingLevel() ),
                                    logging::logDirectory() );

        newWindowAction->setVisible( config.allowMultipleWindows() );
        followAction->setEnabled( config.anyFileWatchEnabled() );

        updateShortcuts();
        updateRecentFileActions();
        refreshSliceOverlayForSelectedLine();
    } );
    dialog.exec();

    signalMux_.disconnect( &dialog, SIGNAL( optionsChanged() ), SLOT( applyConfiguration() ) );
}

void MainWindow::about()
{
    KLOGG_UI_TRACE_INSTANT( "MainWindow::about" );
    QMessageBox::about(
        this, tr( "About klogg" ),
        tr( "<h2>klogg %1</h2>"
            "<p>A fast, advanced log explorer.</p>"
            "<p>Built %2 from %3</p>"
            "<p><a href=\"https://github.com/variar/klogg\">https://github.com/variar/klogg</a></p>"
            "<p>This is fork of glogg</p>"
            "<p><a href=\"http://glogg.bonnefon.org/\">http://glogg.bonnefon.org/</a></p>"
            "<p>Using icons from <a href=\"https://icons8.com\">icons8.com</a> project</p>"
            "<p>Copyright &copy; 2020 Nicolas Bonnefon, Anton Filimonov and other contributors</p>"
            "<p>You may modify and redistribute the program under the terms of the GPL (version 3 "
            "or later).</p>" )
            .arg( kloggUiVersion(), kloggBuildDate(), kloggCommit() ) );
}

void MainWindow::aboutQt()
{
    KLOGG_UI_TRACE_INSTANT( "MainWindow::aboutQt" );
    QMessageBox::aboutQt( this, tr( "About Qt" ) );
}

void MainWindow::documentation()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::documentation" );
    QFile doc( ":/documentation.html" );
    if ( doc.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        const auto text = QString::fromUtf8( doc.readAll() );
        QTextBrowser* tb = new QTextBrowser();
        tb->setOpenExternalLinks( true );
        tb->setHtml( text );
        tb->setWindowFlags( Qt::Window );
        tb->setAttribute( Qt::WA_DeleteOnClose );
        tb->setWindowTitle( tr( "klogg documentation" ) );
        tb->resize( this->width() / 2, this->height() );
        tb->show();
    }
    else {
        LOG_ERROR << "Can't open documentation resource";
    }
}

void MainWindow::showScratchPad()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::showScratchPad" );
    auto state = scratchPad_.windowState();
    state.setFlag( Qt::WindowMinimized, false );
    scratchPad_.setWindowState( state );

    scratchPad_.show();
    scratchPad_.activateWindow();
}

void MainWindow::exportEvents()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::exportEvents" );
    auto* crawler = currentCrawlerWidget();
    if (!crawler) {
        QMessageBox::warning(this, tr("Export Error"), tr("No file is currently open."));
        return;
    }

    const QString& fileName = session_.getFilename(crawler);
    if (fileName.isEmpty()) {
        QMessageBox::warning(this, tr("Export Error"), tr("No file is currently open."));
        return;
    }

    const auto configs = klogg::ui::listLogTypeConfigs();

    // Check for log parsing rules
    if ( !klogg::ui::hasAnyParsingRule( configs ) ) {
        QMessageBox::warning(
            this, tr( "Export Error" ),
            tr( "No log parsing rules configured. Please configure log parsing rules first "
                "to extract timestamp, PID, TID, etc. from log lines.\n\n"
                "Go to Tools > Log Parsing Rules to configure." ) );
        return;
    }

    if ( klogg::ui::allEventRules( configs ).isEmpty() ) {
        QMessageBox::warning(
            this, tr( "Export Error" ),
            tr( "No event rules found. Please create rules first using 'Mark as Event'." ) );
        return;
    }

    prepareEventExport( crawler, fileName, false );
}

void MainWindow::exportAndOpenInPerfettoUI()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::exportAndOpenInPerfettoUI" );
    auto* crawler = currentCrawlerWidget();
    if (!crawler) {
        QMessageBox::warning(this, tr("Export Error"), tr("No file is currently open."));
        return;
    }

    const QString fileName = session_.getFilename(crawler);
    if (fileName.isEmpty()) {
        QMessageBox::warning(this, tr("Export Error"), tr("No file is currently open."));
        return;
    }

    const auto configs = klogg::ui::listLogTypeConfigs();

    if ( !klogg::ui::hasAnyParsingRule( configs ) ) {
        QMessageBox::warning(
            this, tr( "Export Error" ),
            tr( "No log parsing rules configured. Please configure log parsing rules first "
                "to extract timestamp, PID, TID, etc. from log lines.\n\n"
                "Go to Tools > Log Parsing Rules to configure." ) );
        return;
    }

    const auto allRules = klogg::ui::allEventRules( configs );
    if (allRules.isEmpty()) {
        QMessageBox::warning(
            this, tr( "Export Error" ),
            tr( "No event rules found. Please create rules first using 'Mark as Event'." ) );
        return;
    }

    QStringList selectedCategories = getSelectedEventCategories();
    bool hasEnabledRule = false;
    for (const auto& rule : allRules) {
        if (!rule.enabled) continue;
        // Filter by selected categories (empty list means all categories)
        if (!selectedCategories.isEmpty()
            && !selectedCategories.contains(QString::fromStdString(rule.category)))
            continue;
        hasEnabledRule = true;
        break;
    }

    if ( !hasEnabledRule ) {
        QMessageBox::information( this, tr( "Export" ),
                                  tr( "No enabled event rules in the selected categories." ) );
        return;
    }

    prepareEventExport( crawler, fileName, true );
}

void MainWindow::prepareEventExport( CrawlerWidget* crawler, const QString& fileName,
                                     bool openInPerfettoUi )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::prepareEventExport" );
    auto& apiSession = klogg::ui::apiSession();

    // Reuse the crawler's completed extraction when there is one
    std::optional<klogg::api::ExtractionId> existingExtraction;
    if ( const auto current = crawler->currentEventExtractionId() ) {
        const auto progress = apiSession.extractionProgress( *current );
        if ( progress && progress->progress >= 1.0f ) {
            existingExtraction = current;
        }
    }

    std::optional<std::string> configName;
    if ( !existingExtraction ) {
        const auto matched = klogg::ui::matchLogTypeForFile( fileName.toStdString() );
        if ( matched ) {
            configName = matched->name;
        }
        if ( !configName ) {
            QMessageBox::warning(
                this, tr( "Export Error" ),
                tr( "No log parsing rules configured. Please configure log parsing rules first "
                    "to extract timestamp, PID, TID, etc. from log lines.\n\n"
                    "Go to Tools > Log Parsing Rules to configure." ) );
            return;
        }
    }

    // Create progress dialog
    if (perfettoProgressDialog_) {
        delete perfettoProgressDialog_;
    }
    perfettoProgressDialog_ = new QProgressDialog(this);
    perfettoProgressDialog_->setWindowTitle( openInPerfettoUi ? tr( "Export to Perfetto" )
                                                              : tr( "Export Events" ) );
    perfettoProgressDialog_->setLabelText(tr("Preparing event extraction..."));
    perfettoProgressDialog_->setRange(0, 100);
    perfettoProgressDialog_->setValue(0);
    perfettoProgressDialog_->setCancelButton(nullptr);
    perfettoProgressDialog_->setWindowModality(Qt::WindowModal);
    perfettoProgressDialog_->setMinimumDuration(0);
    perfettoProgressDialog_->show();

    // Cosmetic gauge movement while the worker waits for the engine
    auto* progressTimer = new QTimer( this );
    connect( progressTimer, &QTimer::timeout, this, [ this ]() {
        if ( perfettoProgressDialog_ && perfettoProgressDialog_->value() < 90 ) {
            perfettoProgressDialog_->setValue( perfettoProgressDialog_->value() + 5 );
        }
    } );
    progressTimer->start(200);

    // The API is thread-safe: the wait runs on a worker so the GUI stays
    // responsive even when a fresh extraction scans a large file
    const auto fileId = crawler->fileId();
    auto* watcher = new QFutureWatcher<klogg::api::Result<klogg::api::ExtractionId>>( this );
    connect( watcher, &QFutureWatcher<klogg::api::Result<klogg::api::ExtractionId>>::finished, this,
             [ this, watcher, progressTimer, fileName, openInPerfettoUi ]() {
                 progressTimer->stop();
                 progressTimer->deleteLater();

                 const auto prepared = watcher->result();
                 watcher->deleteLater();

                 if ( perfettoProgressDialog_ ) {
                     perfettoProgressDialog_->setValue( 100 );
                     perfettoProgressDialog_->close();
                     perfettoProgressDialog_->deleteLater();
                     perfettoProgressDialog_ = nullptr;
                 }

                 if ( !prepared ) {
                     QMessageBox::warning( this, tr( "Export Error" ),
                                           QString::fromStdString( prepared.error().message ) );
                     return;
                 }

                 continueEventExport( *prepared, fileName, openInPerfettoUi );
             } );

    watcher->setFuture( QtConcurrent::run( [ fileId, existingExtraction, configName ]() {
        auto& session = klogg::ui::apiSession();

        klogg::api::ExtractionId extractionId = 0;
        if ( existingExtraction ) {
            extractionId = *existingExtraction;
        }
        else {
            klogg::api::ExtractionParams params;
            params.logTypeConfigName = *configName;
            const auto started = session.extractEvents( fileId, params );
            if ( !started ) {
                return klogg::api::Result<klogg::api::ExtractionId>(
                    tl::make_unexpected( started.error() ) );
            }
            extractionId = *started;
        }

        for ( ;; ) {
            const auto progress = session.extractionProgress( extractionId );
            if ( !progress ) {
                return klogg::api::Result<klogg::api::ExtractionId>(
                    tl::make_unexpected( progress.error() ) );
            }
            if ( progress->progress >= 1.0f ) {
                return klogg::api::Result<klogg::api::ExtractionId>( extractionId );
            }
            QThread::msleep( 50 );
        }
    } ) );
}

void MainWindow::continueEventExport( klogg::api::ExtractionId extractionId,
                                      const QString& fileName, bool openInPerfettoUi )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::continueEventExport" );
    auto& apiSession = klogg::ui::apiSession();

    const auto progress = apiSession.extractionProgress( extractionId );
    if ( !progress ) {
        QMessageBox::warning( this, tr( "Export Error" ),
                              QString::fromStdString( progress.error().message ) );
        return;
    }
    if ( progress->eventsFound == 0 ) {
        QMessageBox::information( this, tr( "Export" ),
                                  tr( "No events found in the current file." ) );
        return;
    }

    QString outputFile;
    auto format = klogg::api::ExportFormat::Json;
    if ( openInPerfettoUi ) {
        const QString baseName = QFileInfo( fileName ).baseName();
        const QString tempDir
            = QStandardPaths::writableLocation( QStandardPaths::TempLocation ) + "/klogg";
        QDir().mkpath( tempDir );
        const QString timestamp = QDateTime::currentDateTime().toString( "yyyyMMdd_HHmmss" );
        outputFile = QString( "%1/klogg_%2_%3_events.json" ).arg( tempDir, baseName, timestamp );
    }
    else {
        // Filter strings come from the exporter classes so their existing
        // translations stay valid
        const QString jsonFilter = QString::fromStdString( JsonExporter{}.fileFilter() );
        const QString protoFilter = QString::fromStdString( BinaryProtoExporter{}.fileFilter() );
        QString selectedFilter;
        outputFile
            = QFileDialog::getSaveFileName( this, tr( "Export Events" ), QString(),
                                            jsonFilter + ";;" + protoFilter, &selectedFilter );
        if ( outputFile.isEmpty() ) {
            return;
        }
        if ( selectedFilter == protoFilter ) {
            format = klogg::api::ExportFormat::BinaryProto;
        }
    }

    const QString formatName
        = format == klogg::api::ExportFormat::Json ? QString::fromStdString( JsonExporter{}.name() )
                                                   : QString::fromStdString( BinaryProtoExporter{}.name() );

    auto* watcher = new QFutureWatcher<klogg::api::Result<klogg::api::EventExportResult>>( this );
    connect(
        watcher, &QFutureWatcher<klogg::api::Result<klogg::api::EventExportResult>>::finished, this,
        [ this, watcher, outputFile, formatName, openInPerfettoUi ]() {
            const auto result = watcher->result();
            watcher->deleteLater();

            if ( !result ) {
                if ( openInPerfettoUi ) {
                    QMessageBox::warning( this, tr( "Export Error" ),
                                          QString::fromStdString( result.error().message ) );
                }
                else {
                    QMessageBox::critical(
                        this, tr( "Export Error" ),
                        tr( "Could not export events to: %1" ).arg( outputFile ) );
                }
                return;
            }

            if ( !openInPerfettoUi ) {
                // Show warning if some events were skipped
                if ( result->skippedCount > 0 ) {
                    QMessageBox::warning(
                        this, tr( "Export Warning" ),
                        tr( "%1 event(s) were skipped due to invalid or missing timestamp.\n\n"
                            "Please check your log parsing rules to ensure they correctly extract "
                            "timestamps.\n\n"
                            "Supported formats: yyyy-MM-ddTHH:mm:ss, yyyy-MM-dd HH:mm:ss, etc." )
                            .arg( result->skippedCount ) );
                }

                QString message = tr( "Exported %1 event(s) to %2 format." )
                                      .arg( result->eventCount )
                                      .arg( formatName );
                if ( result->skippedCount > 0 ) {
                    message += tr( "\n\n(%1 event(s) skipped due to invalid timestamp)" )
                                   .arg( result->skippedCount );
                }
                QMessageBox::information( this, tr( "Export Success" ), message );
                return;
            }

            // Start trace processor if available
            bool useTraceProcessor = result->hasTraceProcessor;
            if ( useTraceProcessor ) {
                QString traceProcessorPath
                    = QStandardPaths::findExecutable( "trace_processor_shell" );
                if ( traceProcessorPath.isEmpty() ) {
                    QString appDir = QCoreApplication::applicationDirPath();
                    QString bundledPath = appDir + "/trace_processor_shell";
#ifdef Q_OS_WIN
                    bundledPath += ".exe";
#endif
                    if ( QFile::exists( bundledPath ) ) {
                        traceProcessorPath = bundledPath;
                    }
                }

                if ( !traceProcessorPath.isEmpty() ) {
                // Kill existing processes asynchronously
#ifdef Q_OS_WIN
                    QProcess::startDetached(
                        "taskkill", QStringList() << "/F" << "/IM" << "trace_processor_shell.exe" );
#else
                    QProcess::startDetached( "pkill", QStringList()
                                                          << "-f" << "trace_processor_shell" );
#endif

                    QTimer::singleShot( 500, this, [ this, traceProcessorPath, outputFile ]() {
                        QProcess* process = new QProcess( this );
                        process->setProgram( traceProcessorPath );
                        process->setArguments( QStringList() << "-D" << outputFile );

                        connect( process,
                                 QOverload<QProcess::ProcessError>::of( &QProcess::errorOccurred ),
                                 this, [ this, process ]( QProcess::ProcessError ) {
                                     QMessageBox::warning(
                                         this, tr( "Perfetto Error" ),
                                         tr( "Failed to start trace_processor_shell: %1" )
                                             .arg( process->errorString() ) );
                                     process->deleteLater();
                                 } );

                        process->start();
                        if ( !process->waitForStarted( 3000 ) ) {
                            process->deleteLater();
                        }
                    } );
                }
            }

            // Open Perfetto UI in browser
            auto& settings = PersistentInfo::getStore( app_settings{} );
            QString perfettoUrl = QString::fromStdString(
                settings.getString( "perfetto/url", "https://ui.perfetto.dev/" ) );
            QDesktopServices::openUrl( QUrl( perfettoUrl ) );

            // Show result
            QString message;
            QString exportStats = tr( "Export summary:\n"
                                      "- Total trace events: %1\n"
                                      "- Complete events: %2\n"
                                      "- Open begin events: %3\n"
                                      "- Orphan end events: %4" )
                                      .arg( result->eventCount )
                                      .arg( result->completeEventCount )
                                      .arg( result->orphanBeginCount )
                                      .arg( result->orphanEndCount );

            if ( useTraceProcessor ) {
                message = tr( "Exported %1 event(s) to Perfetto format.\n\n"
                              "%2\n\n"
                              "Trace file: %3\n\n"
                              "Perfetto UI opened in browser.\n"
                              "The local trace processor is running on port 12881.\n\n"
                              "Note: Keep klogg open while using Perfetto UI." )
                              .arg( result->eventCount )
                              .arg( exportStats )
                              .arg( outputFile );
            }
            else {
                QString outputDir = QFileInfo( outputFile ).absolutePath();
                QDesktopServices::openUrl( QUrl::fromLocalFile( outputDir ) );

                message = tr( "Exported %1 event(s) to Perfetto format.\n\n"
                              "%2\n\n"
                              "Trace file: %3\n\n"
                              "Perfetto UI opened in browser.\n"
                              "File location opened - drag and drop the JSON file into Perfetto UI "
                              "to view." )
                              .arg( result->eventCount )
                              .arg( exportStats )
                              .arg( outputFile );
            }

            if ( result->skippedCount > 0 ) {
                message += tr( "\n\n(%1 event(s) skipped due to invalid timestamp)" )
                               .arg( result->skippedCount );
            }

            QMessageBox::information( this, tr( "Export to Perfetto" ), message );
        } );

    // Category checkboxes scope the export (pre-A4 semantics). "All
    // Categories" and an empty selection both export everything, matching the
    // setEventCategoryFilter display convention (empty = all).
    const QStringList selectedCategories = getSelectedEventCategories();
    std::optional<std::vector<std::string>> categories;
    if ( !selectedCategories.isEmpty()
         && !( eventCategoryAllAction && eventCategoryAllAction->isChecked() ) ) {
        std::vector<std::string> selected;
        selected.reserve( static_cast<size_t>( selectedCategories.size() ) );
        for ( const auto& category : selectedCategories ) {
            selected.push_back( category.toStdString() );
        }
        categories = std::move( selected );
    }

    watcher->setFuture(
        QtConcurrent::run( [ extractionId, outputFile, format, categories ]() mutable {
            return klogg::ui::apiSession().exportEvents( extractionId, outputFile.toStdString(),
                                                         format, std::move( categories ) );
        } ) );
}

void MainWindow::showEventRulesManager()
{
    KLOGG_UI_TRACE_INSTANT( "MainWindow::showEventRulesManager" );
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Manage Event Rules"));
    dialog->setMinimumSize(850, 500);
    
    auto* layout = new QVBoxLayout(dialog);
    
    auto* label = new QLabel(tr("Event Rules:"), dialog);
    layout->addWidget(label);
    
    auto* eventList = new QTableWidget(dialog);
    eventList->setColumnCount(6);
    eventList->setHorizontalHeaderLabels({tr("Name"), tr("Category"), tr("Type"), tr("Pattern"), tr("Enabled"), tr("Action")});
    eventList->setSelectionBehavior(QAbstractItemView::SelectRows);
    eventList->setSelectionMode(QAbstractItemView::SingleSelection);
    eventList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    
    // Set column widths: Name, Category, Type have fixed widths
    // Enabled and Action have fixed widths, Pattern stretches to fill remaining space
    eventList->setColumnWidth(0, 150);  // Name
    eventList->setColumnWidth(1, 80);   // Category
    eventList->setColumnWidth(2, 70);   // Type
    eventList->setColumnWidth(4, 60);   // Enabled
    eventList->setColumnWidth(5, 70);   // Action
    
    // Make Pattern column (index 3) stretch to fill remaining space
    eventList->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    // Other columns use fixed width
    eventList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    eventList->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    eventList->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    eventList->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    eventList->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    
    layout->addWidget(eventList);
    
    auto* buttonLayout = new QHBoxLayout();
    
    auto* editButton = new QPushButton(tr("Edit"), dialog);
    auto* addButton = new QPushButton(tr("Add"), dialog);
    auto* deleteButton = new QPushButton(tr("Delete Selected"), dialog);
    auto* closeButton = new QPushButton(tr("Close"), dialog);
    
    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(editButton);
    buttonLayout->addWidget(deleteButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    layout->addLayout(buttonLayout);
    
    auto refreshList = [eventList]() {
        eventList->setRowCount(0);

        const auto rules = klogg::ui::allEventRules();
        eventList->setRowCount( static_cast<int>( rules.size() ) );

        for (int i = 0; i < rules.size(); ++i) {
            const auto& rule = rules[i];
            eventList->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(rule.name)));
            eventList->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(rule.category)));
            
            QString typeStr = (rule.eventType == EventType::Instance) ? QObject::tr("Instance") : QObject::tr("Slice");
            eventList->setItem(i, 2, new QTableWidgetItem(typeStr));
            
            QString patternStr;
            if (rule.eventType == EventType::Instance) {
                patternStr = QString::fromStdString(rule.pattern);
            } else {
                patternStr = QObject::tr("Begin: %1\nEnd: %2")
                                 .arg( QString::fromStdString( rule.beginPattern ) )
                                 .arg( QString::fromStdString( rule.endPattern ) );
            }
            eventList->setItem(i, 3, new QTableWidgetItem(patternStr));
            
            auto* checkItem = new QTableWidgetItem();
            checkItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            checkItem->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
            eventList->setItem(i, 4, checkItem);
            
            auto* deleteBtn = new QPushButton(QObject::tr("Delete"), eventList);
            QString ruleName = QString::fromStdString( rule.name );
            QObject::connect( deleteBtn, &QPushButton::clicked, [ eventList, ruleName ]() {
                klogg::ui::removeEventRule( ruleName );

                for (int row = 0; row < eventList->rowCount(); ++row) {
                    if (eventList->item(row, 0)->text() == ruleName) {
                        eventList->removeRow(row);
                        break;
                    }
                }
            } );
            eventList->setCellWidget(i, 5, deleteBtn);
        }
    };

    auto persistRule = [ dialog ]( const EventRule& editedRule, const QString& oldName ) {
        Q_UNUSED( dialog );

        const auto editedName = QString::fromStdString( editedRule.name );
        if (!oldName.isEmpty() && oldName != editedName) {
            klogg::ui::removeEventRule( oldName );
            klogg::ui::addEventRule( editedRule );
        }
        else {
            klogg::ui::updateEventRule( oldName.isEmpty() ? editedName : oldName, editedRule );
        }
    };

    auto openRuleEditor = [dialog, &refreshList, persistRule, this](EventDialogMode mode,
                                                                    const EventRule* existingRule,
                                                                    const QString& oldName) {
        auto* configDialog = new EventConfigDialog(mode, dialog);
        if (existingRule != nullptr) {
            configDialog->setEventRule(*existingRule);
        }
        configDialog->setAttribute(Qt::WA_DeleteOnClose);

        QObject::connect(configDialog, &QDialog::accepted, dialog, [configDialog, &refreshList, persistRule, oldName]() {
            EventRule editedRule = configDialog->getEventRule();
            persistRule(editedRule, oldName);
            refreshList();
        });

        QObject::connect(configDialog, &QDialog::accepted, this, [this]() {
            updateEventCategoryMenu();
        });

        configDialog->show();
    };
    
    refreshList();
    
    QObject::connect(eventList, &QTableWidget::doubleClicked, [openRuleEditor](const QModelIndex& index) {
        int row = index.row();
        if (row < 0) return;

        const auto rules = klogg::ui::allEventRules();
        if (row >= rules.size()) return;
        
        const EventRule& rule = rules[row];
        
        EventDialogMode mode = (rule.eventType == EventType::Slice) ? EventDialogMode::EditSlice : EventDialogMode::Instance;
        
        openRuleEditor(mode, &rule, QString::fromStdString( rule.name ));
    });
    
    QObject::connect(editButton, &QPushButton::clicked, [eventList, openRuleEditor]() {
        int currentRow = eventList->currentRow();
        if (currentRow < 0) return;

        const auto rules = klogg::ui::allEventRules();
        if (currentRow >= rules.size()) return;
        
        const EventRule& rule = rules[currentRow];
        
        EventDialogMode mode = (rule.eventType == EventType::Slice) ? EventDialogMode::EditSlice : EventDialogMode::Instance;
        
        openRuleEditor(mode, &rule, QString::fromStdString( rule.name ));
    });
    
    QObject::connect(deleteButton, &QPushButton::clicked, [eventList]() {
        int currentRow = eventList->currentRow();
        if (currentRow < 0) return;

        QString ruleName = eventList->item(currentRow, 0)->text();

        klogg::ui::removeEventRule( ruleName );

        eventList->removeRow(currentRow);
    });
    
    QObject::connect(addButton, &QPushButton::clicked, [openRuleEditor]() {
        openRuleEditor(EventDialogMode::Instance, nullptr, QString());
    });
    
    QObject::connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);
    
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::onEventVisibilityRequested()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::onEventVisibilityRequested" );
    // Event extraction is handled by CrawlerWidget::performEventSearch
    // This slot is kept for future extensions if needed
    auto* crawler = currentCrawlerWidget();
    if (!crawler) return;

    if ( klogg::ui::allEventRules().isEmpty() ) {
        statusBar()->showMessage(tr("No event rules. Use 'Mark as Event' to create rules."), 5000);
    }
}

void MainWindow::onVisibilityChanged( bool includesEvents )
{
    Q_UNUSED( includesEvents )

    // Event categories define the Events dataset and remain configurable
    // even when Events are not part of the current projection.
    if ( eventCategoryButton ) {
        eventCategoryButton->setEnabled( currentCrawlerWidget() != nullptr );
    }
}

void MainWindow::onEventCategoryChanged( QAction* triggeredAction )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::onEventCategoryChanged" );
    // Handle select-all logic for "All Categories" and individual categories
    // triggeredAction comes from QMenu::triggered signal
    
    if (triggeredAction == eventCategoryAllAction) {
        // "All Categories" was toggled
        bool isChecked = eventCategoryAllAction->isChecked();
        // Check/uncheck all individual categories to match
        for (QAction* action : eventCategoryMenu->actions()) {
            if (action != eventCategoryAllAction && action->isCheckable() && !action->data().isNull()) {
                action->setChecked(isChecked);
            }
        }
    } else if (triggeredAction != nullptr && triggeredAction->isCheckable() && !triggeredAction->data().isNull()) {
        // An individual category was toggled
        // Update "All Categories" state: only check it when ALL individual categories are checked
        // Allow zero selection (no auto-select to "All Categories")
        
        int checkedCount = 0;
        int categoryCount = 0;
        for (QAction* action : eventCategoryMenu->actions()) {
            if (action != eventCategoryAllAction && action->isCheckable() && !action->data().isNull()) {
                categoryCount++;
                if (action->isChecked()) {
                    checkedCount++;
                }
            }
        }
        
        if (eventCategoryAllAction) {
            // Only check "All Categories" when ALL categories are checked
            // Uncheck when partial or none selected
            eventCategoryAllAction->setChecked(checkedCount > 0 && checkedCount == categoryCount);
        }
    }
    
    // Update button text
    updateEventCategoryButtonText();
    
    QStringList selectedCategories = getSelectedEventCategories();
    
    // Update all crawlers with the new categories
    for (int i = 0; i < mainTabWidget_.count(); ++i) {
        auto* crawler = qobject_cast<CrawlerWidget*>(mainTabWidget_.widget(i));
        if (crawler) {
            crawler->setCurrentEventCategories(selectedCategories);
        }
    }
    
    // Update the current crawler's view
    auto* crawler = currentCrawlerWidget();
    if (crawler) {
        crawler->updateEventView();
    }
}

QStringList MainWindow::getSelectedEventCategories() const
{
    QStringList categories;
    if (eventCategoryMenu == nullptr) {
        return categories;
    }
    
    // Collect checked categories (filter out separator and "All Categories")
    for (QAction* action : eventCategoryMenu->actions()) {
        if (action != eventCategoryAllAction 
            && action->isCheckable() 
            && action->isChecked() 
            && !action->data().isNull()) {
            categories.append(action->data().toString());
        }
    }
    
    return categories;
}

void MainWindow::updateEventCategoryMenu()
{
    const auto rules = klogg::ui::allEventRules();

    // Event rules may have changed: cached extractions no longer reflect them
    for ( int i = 0; i < mainTabWidget_.count(); ++i ) {
        if ( auto* crawler = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( i ) ) ) {
            crawler->invalidateEventExtraction();
        }
    }

    // Collect unique categories
    QSet<QString> categories;
    for (const auto& rule : rules) {
        if (!rule.category.empty()) {
            categories.insert(QString::fromStdString(rule.category));
        }
    }

    // Remember current selection
    QStringList currentCategories = getSelectedEventCategories();

    // Block signals while updating
    eventCategoryMenu->blockSignals(true);
    
    // Clear existing actions except "All Categories"
    QList<QAction*> actionsToRemove;
    for (QAction* action : eventCategoryMenu->actions()) {
        if (action != eventCategoryAllAction) {
            actionsToRemove.append(action);
        }
    }
    for (QAction* action : actionsToRemove) {
        eventCategoryMenu->removeAction(action);
        action->deleteLater();
    }

    // Add categories in sorted order
    QList<QString> sortedCategories = categories.values();
    std::sort(sortedCategories.begin(), sortedCategories.end());
    
    // Determine if all categories should be checked (All Categories mode)
    bool allSelected = currentCategories.isEmpty();
    
    for (const QString& cat : sortedCategories) {
        QAction* action = eventCategoryMenu->addAction(cat);
        action->setCheckable(true);
        action->setData(cat);
        
        // In select-all mode, check all items when "All Categories" is checked
        if (allSelected) {
            action->setChecked(true);
        } else if (currentCategories.contains(cat)) {
            action->setChecked(true);
        }
        
        // No need to connect toggled signal - QMenu::triggered handles all clicks
    }

    // Update "All Categories" state
    if (eventCategoryAllAction) {
        eventCategoryAllAction->setChecked(allSelected);
    }
    
    eventCategoryMenu->blockSignals(false);
    
    // Update button text
    updateEventCategoryButtonText();
}

void MainWindow::updateEventCategoryButtonText()
{
    if (eventCategoryButton == nullptr) {
        return;
    }
    
    // Check "All Categories" state
    if (eventCategoryAllAction && eventCategoryAllAction->isChecked()) {
        eventCategoryButton->setText(tr("All Categories"));
        return;
    }
    
    QStringList selected = getSelectedEventCategories();
    
    if (selected.isEmpty()) {
        // No categories selected - user explicitly deselected all
        eventCategoryButton->setText(tr("No Categories"));
    } else if (selected.size() == 1) {
        eventCategoryButton->setText(selected.first());
    } else {
        eventCategoryButton->setText(tr("%1 Categories").arg(selected.size()));
    }
}

void MainWindow::showLogParsingRulesDialog()
{
    KLOGG_UI_TRACE_INSTANT( "MainWindow::showLogParsingRulesDialog" );
    auto* dialog = new LogParsingRulesDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::sendToScratchpad( QString newData )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::sendToScratchpad" );
    scratchPad_.addData( newData );
    showScratchPad();
}

void MainWindow::replaceDataInScratchpad( QString newData )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::replaceDataInScratchpad" );
    scratchPad_.replaceData( newData );
    showScratchPad();
}

void MainWindow::encodingChanged( QAction* action )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::encodingChanged" );
    const auto mibData = action->data();
    std::optional<int> mib;
    if ( mibData.isValid() ) {
        mib = mibData.toInt();
    }

    LOG_DEBUG << "encodingChanged, encoding " << mib.value_or( 0 );
    if ( auto crawler = currentCrawlerWidget() ) {
        crawler->setEncoding( mib );
        updateInfoLine();
    }
}

void MainWindow::toggleOverviewVisibility( bool isVisible )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::toggleOverviewVisibility" );
    auto& config = Configuration::get();
    config.setOverviewVisible( isVisible );
    config.save();
    Q_EMIT optionsChanged();
}

void MainWindow::toggleMainLineNumbersVisibility( bool isVisible )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::toggleMainLineNumbersVisibility" );
    auto& config = Configuration::get();

    config.setMainLineNumbersVisible( isVisible );
    config.save();
    Q_EMIT optionsChanged();
}

void MainWindow::toggleFilteredLineNumbersVisibility( bool isVisible )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::toggleFilteredLineNumbersVisibility" );
    auto& config = Configuration::get();

    config.setFilteredLineNumbersVisible( isVisible );
    config.save();
    Q_EMIT optionsChanged();
}

void MainWindow::changeFollowMode( bool follow )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::changeFollowMode" );
    auto& config = Configuration::get();
    if ( follow && !( config.nativeFileWatchEnabled() || config.pollingEnabled() ) ) {
        LOG_WARNING << "File watch disabled in settings";
    }

    followAction->setChecked( follow );
}

void MainWindow::lineNumberHandler( LineNumber startLine, LinesCount nLines, LineColumn startCol,
                                    LineLength nSymbols )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::lineNumberHandler" );
    // The line number received is the internal (starts at 0)
    uint64_t fileSize{};
    uint64_t fileNbLine{};
    QDateTime lastModified;

    session_.getFileInfo( currentCrawlerWidget(), &fileSize, &fileNbLine, &lastModified );

    if ( fileNbLine != 0 ) {
        if ( nSymbols.get() == 0 ) {
            lineNbField->setText( tr( "Ln:%1/%2" ).arg( startLine.get() + 1 ).arg( fileNbLine ) );
        }
        else {
            if ( nLines.get() == 1 ) {
                // portion selection on one line
                lineNbField->setText( tr( "Ln:%1/%2 Col:%3 Sel:%4|%5" )
                                          .arg( startLine.get() + 1 )
                                          .arg( fileNbLine )
                                          .arg( startCol.get() )
                                          .arg( nSymbols.get() )
                                          .arg( nLines.get() ) );
            }
            else {
                // multiple lines selection
                lineNbField->setText( tr( "Ln:%1/%2 Sel:%4|%5" )
                                          .arg( startLine.get() + 1 )
                                          .arg( fileNbLine )
                                          .arg( nSymbols.get() )
                                          .arg( nLines.get() ) );
            }
        }
    }
    else {
        lineNbField->clear();
    }
}

void MainWindow::newPredefinedFilterHandler( QString newFilter, bool useRegex )
{
    editPredefinedFilters( newFilter, useRegex );
}

void MainWindow::updateLoadingProgress( int progress )
{
    LOG_DEBUG << "Loading progress: " << progress;

    QString current_file
        = QDir::toNativeSeparators( session_.getFilename( currentCrawlerWidget() ) );

    // We ignore 0% and 100% to avoid a flash when the file (or update)
    // is very short.
    if ( progress > 0 && progress < 100 ) {
        infoLine->setText( current_file + tr( " - Indexing lines... (%1 %)" ).arg( progress ) );
        infoLine->displayGauge( progress );

        showInfoLabels( false );

        stopAction->setEnabled( true );
        reloadAction->setEnabled( false );
    }
}

void MainWindow::handleLoadingFinished( LoadingStatus status )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::handleLoadingFinished" );
    LOG_DEBUG << "handleLoadingFinished success=" << ( status == LoadingStatus::Successful );

    // No file is loading
    loadingFileName.clear();

    if ( status == LoadingStatus::Successful ) {
        updateInfoLine();

        infoLine->hideGauge();
        showInfoLabels( true );
        stopAction->setEnabled( false );
        reloadAction->setEnabled( true );

        lineNumberHandler( 0_lnum, LinesCount( 0 ), LineColumn( 0 ), LineLength( 0 ) );

        // Now everything is ready, we can finally show the file!
        currentCrawlerWidget()->show();

        if ( pendingTopLine_ > 0 ) {
            currentCrawlerWidget()->scrollToLine( LineNumber( pendingTopLine_ ) );
            pendingTopLine_ = 0;
        }
        if ( !pendingSearchText_.isEmpty() ) {
            currentCrawlerWidget()->setSearchText( pendingSearchText_ );
            pendingSearchText_.clear();
        }
    }
    else {
        if ( status == LoadingStatus::NoMemory ) {
            QMessageBox alertBox;
            alertBox.setText( tr( "Not enough memory." ) );
            alertBox.setInformativeText(
                tr( "The system does not have enough memory to hold the index for this file. The "
                    "file will now be closed." ) );
            alertBox.setIcon( QMessageBox::Critical );
            alertBox.exec();
        }

        closeTab( mainTabWidget_.currentIndex(), ActionInitiator::App );
    }

    // mainTabWidget_.setEnabled( true );
}

void MainWindow::handleFilteredViewChanged()
{
    int currentIndex = mainTabWidget_.currentIndex();
    if ( currentIndex >= 0 ) {
        auto* crawler_widget = static_cast<CrawlerWidget*>( mainTabWidget_.widget( currentIndex ) );
        quickFindMux_.registerSelector( crawler_widget );
    }
}

void MainWindow::closeTab( int index, ActionInitiator initiator )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::closeTab" );
    auto widget = qobject_cast<CrawlerWidget*>( mainTabWidget_.widget( index ) );

    assert( widget );

    if ( recorderManager_.isRecording()
         && session_.getFilename( widget ) == recordingFileName_ ) {
        recorderManager_.stop();
    }

    // A shared file ("open in new window") keeps indexing for the other
    // views; interrupting its loading would surface as an interrupted status
    // there and close those tabs
    if ( !session_.isShared( widget ) ) {
        widget->stopLoading();
    }
    mainTabWidget_.removeCrawler( index );

    if ( initiator == ActionInitiator::User ) {
        addRecentFile( session_.getFilename( widget ) );
    }

    session_.close( widget );

    updateOpenedFilesMenu();

    widget->deleteLater();
}

void MainWindow::currentTabChanged( int index )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::currentTabChanged" );
    LOG_INFO << "currentTabChanged: index=" << index << " tabCount=" << mainTabWidget_.count();

    if ( index >= 0 ) {
        auto* crawler_widget = static_cast<CrawlerWidget*>( mainTabWidget_.widget( index ) );
        signalMux_.setCurrentDocument( crawler_widget );
        quickFindMux_.registerSelector( crawler_widget );

        if (crawler_widget != nullptr) {
            LOG_INFO << "currentTabChanged: crawler available, eventVisibility="
                     << crawler_widget->isEventVisibility();
            onVisibilityChanged(crawler_widget->isEventVisibility());
            overlaySelectedLine_.reset();
            refreshSliceOverlayForSelectedLine();
        }

        // New tab is set up with fonts etc...
        Q_EMIT optionsChanged();

        updateMenuBarFromDocument( crawler_widget );

        // Apply global chip mode state to this crawler
        crawler_widget->setChipMode( globalChipMode_ );

        updateTitleBar( session_.getFilename( crawler_widget ) );
        updateFavoritesMenu();

        editMenu->setEnabled( true );

        updateRecordActionState();
    }
    else {
        LOG_INFO << "currentTabChanged: no active tab, hiding overlay";
        // No tab left
        signalMux_.setCurrentDocument( nullptr );
        quickFindMux_.registerSelector( nullptr );

        infoLine->hideGauge();
        infoLine->clear();
        showInfoLabels( false );

        updateTitleBar( QString() );

        editMenu->setEnabled( false );
        addToFavoritesAction->setEnabled( false );
        addToFavoritesMenuAction->setEnabled( false );
        recordAction->setEnabled( false );
        onVisibilityChanged(false);
        if ( sliceOverlayWidget_ ) {
            sliceOverlayWidget_->hide();
        }
    }
}

bool MainWindow::isSliceOverlayEnabled() const
{
    return sliceOverlayEnabled_;
}

void MainWindow::setSliceOverlayEnabled( bool enabled )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::setSliceOverlayEnabled" );
    LOG_DEBUG << "setSliceOverlayEnabled: enabled=" << enabled;
    sliceOverlayEnabled_ = enabled;

    PersistentInfo::getStore( app_settings{} )
        .setBool( "eventVisualization/sliceHoverOverlayEnabled", enabled );
    PersistentInfo::saveAppStore();

    if ( visualizeInKloggAction != nullptr && visualizeInKloggAction->isChecked() != enabled ) {
        visualizeInKloggAction->setChecked( enabled );
    }

    refreshSliceOverlayForSelectedLine();
}

QAction* MainWindow::visualizeInKloggActionForTest() const
{
    return visualizeInKloggAction;
}

bool MainWindow::isSliceOverlayVisibleForTest() const
{
    return sliceOverlayWidget_ != nullptr && sliceOverlayWidget_->isVisible();
}

void MainWindow::onSelectedSourceLineChanged( LineNumber line )
{
    LOG_DEBUG << "onSelectedSourceLineChanged: line=" << line.get();
    overlaySelectedLine_ = line;
    refreshSliceOverlayForSelectedLine();
}

void MainWindow::refreshSliceOverlayForSelectedLine()
{
    if ( !sliceOverlayWidget_ ) {
        LOG_WARNING << "refreshSliceOverlayForSelectedLine: missing overlay widget";
        return;
    }

    LOG_DEBUG << "refreshSliceOverlayForSelectedLine: enabled=" << isSliceOverlayEnabled()
              << " selectedLine="
              << ( overlaySelectedLine_.has_value() ? QString::number( overlaySelectedLine_->get() )
                                                    : QStringLiteral( "<none>" ) );

    if ( !isSliceOverlayEnabled() ) {
        LOG_DEBUG << "refreshSliceOverlayForSelectedLine: hiding overlay because feature disabled";
        sliceOverlayWidget_->hide();
        return;
    }

    auto* crawler = currentCrawlerWidget();
    if ( !crawler ) {
        LOG_DEBUG << "refreshSliceOverlayForSelectedLine: hiding overlay because no crawler";
        sliceOverlayWidget_->hide();
        return;
    }

    auto overlayFont = Configuration::get().mainFont();
    overlayFont.setKerning( false );
    overlayFont.setFixedPitch( true );
    overlayFont.setBold( Configuration::get().useBoldFont() );
    if ( Configuration::get().forceFontAntialiasing() ) {
        overlayFont.setStyleStrategy( QFont::PreferAntialias );
    }
    sliceOverlayWidget_->setFont( overlayFont );
    sliceOverlayWidget_->setTextColor( Configuration::get().sliceOverlayTextColor() );
    sliceOverlayWidget_->setBackgroundColor( Configuration::get().sliceOverlayBackgroundColor() );
    sliceOverlayWidget_->setBorderEnabled( Configuration::get().sliceOverlayBorderEnabled() );

    if ( !overlaySelectedLine_.has_value() ) {
        LOG_DEBUG << "refreshSliceOverlayForSelectedLine: showing empty overlay without selected line";
        sliceOverlayWidget_->showEmptyState( 0_lnum );
        updateSliceOverlayPosition();
        sliceOverlayWidget_->show();
        sliceOverlayWidget_->raise();
        LOG_DEBUG << "refreshSliceOverlayForSelectedLine: overlay visible="
                  << sliceOverlayWidget_->isVisible();
        return;
    }

    const auto slices = crawler->slicesAtSourceLine( *overlaySelectedLine_ );
    LOG_DEBUG << "refreshSliceOverlayForSelectedLine: selectedLine=" << overlaySelectedLine_->get()
              << " slices=" << slices.size();
    if ( slices.isEmpty() ) {
        sliceOverlayWidget_->showEmptyState( *overlaySelectedLine_ );
    }
    else {
        sliceOverlayWidget_->setSlices( slices, *overlaySelectedLine_ );
    }

    updateSliceOverlayPosition();
    sliceOverlayWidget_->show();
    sliceOverlayWidget_->raise();
    LOG_DEBUG << "refreshSliceOverlayForSelectedLine: overlay visible=" << sliceOverlayWidget_->isVisible();
}

void MainWindow::updateSliceOverlayPosition()
{
    if ( !sliceOverlayWidget_ ) {
        LOG_WARNING << "updateSliceOverlayPosition: missing overlay widget";
        return;
    }

    const int margin = 16;
    QRect overlayAnchorRect;
    if ( auto* crawler = currentCrawlerWidget(); crawler != nullptr ) {
        const QRect crawlerAnchorRect = crawler->sliceOverlayAnchorRect();
        if ( crawlerAnchorRect.isValid() && !crawlerAnchorRect.isEmpty() ) {
            overlayAnchorRect = QRect( crawler->mapTo( this, crawlerAnchorRect.topLeft() ),
                                       crawlerAnchorRect.size() );
        }
    }

    if ( !overlayAnchorRect.isValid() || overlayAnchorRect.isEmpty() ) {
        const int fallbackTop = toolBar != nullptr ? toolBar->geometry().bottom() + 1 : 0;
        overlayAnchorRect = QRect( margin, fallbackTop + margin, qMax( 0, width() - 2 * margin ),
                                   qMax( 0, height() - fallbackTop - 2 * margin ) );
        LOG_DEBUG << "updateSliceOverlayPosition: using fallback anchor rect="
                  << overlayAnchorRect.x() << "," << overlayAnchorRect.y() << ","
                  << overlayAnchorRect.width() << "," << overlayAnchorRect.height();
    }

    const int preferredWidth = qMax( 360, overlayAnchorRect.width() / 6 );

    sliceOverlayWidget_->setPreferredDisplayWidth( preferredWidth );
    sliceOverlayWidget_->setMaximumDisplayHeight( qMax( 140, overlayAnchorRect.height() / 3 ) );

    const int actualOverlayWidth = qMax( sliceOverlayWidget_->width(), preferredWidth );
    const int x = overlayAnchorRect.right() - actualOverlayWidth - margin + 1;
    const int y = overlayAnchorRect.top() + margin;
    const int finalX = qMax( overlayAnchorRect.left() + margin, x );
    const int finalY = qMax( margin, y );

    sliceOverlayWidget_->move( finalX, finalY );
    LOG_DEBUG << "updateSliceOverlayPosition: window=" << width() << "x" << height()
              << " preferredWidth=" << preferredWidth << " actualOverlayWidth="
              << actualOverlayWidth << " anchorRect=" << overlayAnchorRect.x() << ","
              << overlayAnchorRect.y() << "," << overlayAnchorRect.width() << ","
              << overlayAnchorRect.height() << " rawPos=" << x << "," << y << " finalPos="
              << finalX << "," << finalY << " overlayGeometry="
              << sliceOverlayWidget_->geometry().x() << "," << sliceOverlayWidget_->geometry().y()
              << "," << sliceOverlayWidget_->geometry().width() << ","
              << sliceOverlayWidget_->geometry().height();
}

void MainWindow::changeQFPattern( const QString& newPattern )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::changeQFPattern" );
    quickFindWidget_.changeDisplayedPattern( newPattern, true );
}

void MainWindow::loadFileFromAnotherWindow( const QString& fileName, uint64_t topLine,
                                          const QString& viewContext, const QString& searchText )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::loadFileFromAnotherWindow" );
    LOG_DEBUG << "loadFileFromAnotherWindow( " << fileName.toStdString() << " )";

    pendingTopLine_ = topLine;
    pendingSearchText_ = searchText;

    if ( !loadFileInternal( fileName, viewContext ) ) {
        pendingTopLine_ = 0;
        pendingSearchText_.clear();
    }
}

void MainWindow::loadFileNonInteractive( const QString& file_name )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::loadFileNonInteractive" );
    LOG_DEBUG << "loadFileNonInteractive( " << file_name.toStdString() << " )";

    loadFile( file_name );

    // Try to get the window to the front
    // This is a bit of a hack but has been tested on:
    // Qt 5.3 / Gnome / Linux
    // Qt 5.11 / Win10
#ifdef Q_OS_WIN
    const auto isMaximized = isMaximized_;

    if ( isMaximized ) {
        showMaximized();
    }
    else {
        showNormal();
    }

    activateWindow();
    raise();
#else
    Qt::WindowFlags window_flags = windowFlags();
    window_flags |= Qt::WindowStaysOnTopHint;
    setWindowFlags( window_flags );

    raise();
    activateWindow();

    window_flags = windowFlags();
    window_flags &= ~Qt::WindowStaysOnTopHint;
    setWindowFlags( window_flags );
    show();
#endif

    if ( auto currentCrawler = currentCrawlerWidget() ) {
        currentCrawler->setFocus();
    }
}

void MainWindow::openCurrentFileInNewWindow()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::openCurrentFileInNewWindow" );
    auto* crawler = currentCrawlerWidget();
    if ( !crawler ) {
        return;
    }

    const auto fileName = session_.getFilename( crawler );
    if ( fileName.isEmpty() ) {
        return;
    }

    const auto topLine = crawler->getTopLine().get();
    const auto viewContext = crawler->context()->toString();
    const auto searchText = crawler->getCurrentSearchText();

    Q_EMIT openFileInNewWindow( fileName, topLine, viewContext, searchText );
}

//
// Events
//

// Closes the application
void MainWindow::closeEvent( QCloseEvent* event )
{
    if ( !isCloseFromTray_ && this->isVisible() && Configuration::get().minimizeToTray() ) {
        event->ignore();
        trayIcon_->show();
        this->hide();
    }
    else {
        const auto saveSettings = session_.close();
        if ( saveSettings ) {
            writeSettings();
        }

        closeAll( ActionInitiator::App );
        trayIcon_->hide();
        Q_EMIT windowClosed();

        event->accept();
    }
}

// Minimize handling the application
void MainWindow::changeEvent( QEvent* event )
{
    if ( event->type() == QEvent::WindowStateChange ) {
        isMaximized_ = windowState().testFlag( Qt::WindowMaximized );

        if ( this->windowState() & Qt::WindowMinimized ) {
            if ( Configuration::get().minimizeToTray() ) {
                dispatchToMainThread( [ this ] {
                    trayIcon_->show();
                    this->hide();
                } );
            }
        }
    }
    else if ( event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange ) {
        dispatchToMainThread( [ this ] {
            loadIcons();
            updateOpenedFilesMenu();
            updateFavoritesMenu();
            updateHighlightersMenu();
        } );
    }
    else if ( event->type() == QEvent::LanguageChange ) {
        reTranslateUI();
    }

    QMainWindow::changeEvent( event );
}

void MainWindow::resizeEvent( QResizeEvent* event )
{
    QMainWindow::resizeEvent( event );
    updateSliceOverlayPosition();
}

// Accepts the drag event if it looks like a filename
void MainWindow::dragEnterEvent( QDragEnterEvent* event )
{
    if ( event->mimeData()->hasFormat( "text/uri-list" ) )
        event->acceptProposedAction();
}

// Tries and loads the file if the URL dropped is local
void MainWindow::dropEvent( QDropEvent* event )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::dropEvent" );
    const QList<QUrl> urls = event->mimeData()->urls();

    for ( const auto& url : urls ) {
        auto fileName = url.toLocalFile();
        if ( fileName.isEmpty() )
            continue;

        loadFile( fileName );
    }
}

bool MainWindow::event( QEvent* event )
{
    if ( event->type() == QEvent::WindowActivate ) {
        Q_EMIT windowActivated();
    }
    else if ( event->type() == QEvent::Show ) {
        if ( this->windowHandle() ) {
            std::call_once( screenChangesConnect_, [ this ]() {
                logScreenInfo( this->windowHandle()->screen() );
                connect( this->windowHandle(), &QWindow::screenChanged,
                         [ this ]( QScreen* screen ) { logScreenInfo( screen ); } );
            } );
        }
    }

    return QMainWindow::event( event );
}

bool MainWindow::eventFilter( QObject* watched, QEvent* event )
{
    // Prevent menu from closing when clicking checkable category items
    if (watched == eventCategoryMenu && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        QAction* action = eventCategoryMenu->actionAt(mouseEvent->pos());
        
        // Keep menu open for individual category items (not "All Categories")
        if (action && action->isCheckable() && !action->data().isNull()) {
            // Toggle the action manually
            action->setChecked(!action->isChecked());
            // Trigger the changed logic
            onEventCategoryChanged(action);
            // Don't pass to base - this prevents menu from closing
            return true;
        }
    }
    
    return QMainWindow::eventFilter(watched, event);
}

//
// Private functions
//

bool MainWindow::extractAndLoadFile( const QString& fileName )
{
    // Decompression runs under a modal progress dialog; the scope measures
    // the whole user-visible extraction
    KLOGG_UI_TRACE_SCOPE( "MainWindow::extractAndLoadFile" );
    const auto& config = Configuration::get();

    if ( !config.extractArchives() ) {
        return false;
    }

    if ( !config.extractArchivesAlways() ) {
        const auto userChoice
            = QMessageBox::question( this, tr( "klogg" ), tr( "Extract archive to temp folder?" ) );
        if ( userChoice == QMessageBox::No ) {
            return false;
        }
    }

    const auto decompressAction = Decompressor::action( fileName );

    Decompressor decompressor;
    QAtomicInt decompressInterrupt;

    QProgressDialog progressDialog;
    progressDialog.setLabelText( tr( "Extracting %1" ).arg( fileName ) );
    progressDialog.setRange( 0, 0 );

    connect( &decompressor, &Decompressor::finished,
             [ &progressDialog ]( bool isOk ) { progressDialog.done( isOk ? 0 : 1 ); } );
    connect( &progressDialog, &QProgressDialog::canceled,
             [ &decompressInterrupt, &decompressor ]() {
                 decompressInterrupt.storeRelease( 1 );
                 decompressor.waitForResult();
             } );

    if ( decompressAction == DecompressAction::Decompress ) {

        auto tempFile = new QTemporaryFile(
            QDir( currentTempRoot() ).filePath( QFileInfo( fileName ).fileName() ), this );

        if ( tempFile->open() && decompressor.decompress( fileName, tempFile, decompressInterrupt )
             && !progressDialog.exec() ) {

            if ( decompressInterrupt.loadAcquire() != 0 ) {
                return false;
            }

            return this->loadFile( tempFile->fileName() );
        }
        else {
            QMessageBox::warning(
                this, tr( "klogg" ),
                tr( "Failed to decompress %1" ).arg( QDir::toNativeSeparators( fileName ) ) );
        }
    }
    else if ( decompressAction == DecompressAction::Extract ) {
        auto archiveDirPtr = std::make_unique<QTemporaryDir>(
            QDir( currentTempRoot() ).filePath( QFileInfo( fileName ).fileName() ) );
        const QString archiveDirPath = archiveDirPtr->path();
        archiveDirs_.push_back( std::move( archiveDirPtr ) );
        if ( decompressor.extract( fileName, archiveDirPath, decompressInterrupt )
             && !progressDialog.exec() ) {

            if ( decompressInterrupt.loadAcquire() != 0 ) {
                return false;
            }

            const auto selectedFiles = QFileDialog::getOpenFileNames(
                this, tr( "Open file from archive" ), archiveDirPath, tr( "All files (*)" ) );

            for ( const auto& extractedFile : selectedFiles ) {
                this->loadFile( extractedFile );
            }

            return true;
        }
        else {
            QMessageBox::warning(
                this, tr( "klogg" ),
                tr( "Failed to extract %1" ).arg( QDir::toNativeSeparators( fileName ) ) );
        }
    }

    return false;
}

// Create a CrawlerWidget for the passed file, start its loading
// and update the title bar.
// The loading is done asynchronously.
bool MainWindow::loadFile( const QString& fileName, bool followFile )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::loadFile" );
    LOG_DEBUG << "loadFile ( " << fileName.toStdString() << " )";

    // First check if the file is already open...
    auto* existing_crawler = static_cast<CrawlerWidget*>( session_.getViewIfOpen( fileName ) );

    if ( existing_crawler ) {
        auto* crawlerWindow = qobject_cast<MainWindow*>( existing_crawler->window() );
        crawlerWindow->mainTabWidget_.setCurrentWidget( existing_crawler );
        crawlerWindow->activateWindow();
        return true;
    }

    const auto decompressAction = Decompressor::action( fileName );

    if ( decompressAction == DecompressAction::None || !Configuration::get().extractArchives() ) {
        // Load the file
        const auto previousViewContext = [ &fileName ]() {
            const auto& session = SessionInfo::getSynced();
            const auto& windows = session.windows();
            for ( const auto& windowId : windows ) {
                const auto openedFiles = session.openFiles( windowId );
                const auto existingContext
                    = std::find_if( openedFiles.begin(), openedFiles.end(),
                                    [ &fileName ]( const auto& context ) {
                                        return context.fileName == fileName;
                                    } );
                if ( existingContext != openedFiles.end() ) {
                    return existingContext->viewContext;
                }
            }
            return QString{};
        }();

        if ( loadFileInternal( fileName, previousViewContext ) ) {
            const auto& config = Configuration::get();
            if ( config.anyFileWatchEnabled() && ( followFile || config.followFileOnLoad() ) ) {
                auto* crawler = currentCrawlerWidget();
                if ( crawler ) {
                    signalCrawlerToFollowFile( crawler );
                    followAction->setChecked( true );
                }
            }
            return true;
        }
        return false;
    }

    return extractAndLoadFile( fileName );
}

bool MainWindow::loadFileInternal( const QString& fileName, const QString& viewContext )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::loadFileInternal" );
    loadingFileName = fileName;

    try {
        CrawlerWidget* crawler_widget = static_cast<CrawlerWidget*>(
            session_.openAlways( fileName, []() { return new CrawlerWidget(); }, viewContext ) );

        if ( !crawler_widget ) {
            LOG_ERROR << "Can't create crawler for " << fileName.toStdString();
            return false;
        }

        // We won't show the widget until the file is fully loaded
        crawler_widget->hide();

        int index = mainTabWidget_.addCrawler( crawler_widget, fileName );

        // Apply global chip mode to new crawler
        crawler_widget->setChipMode( globalChipMode_ );

        mainTabWidget_.setCurrentIndex( index );

        addRecentFile( fileName );
        updateOpenedFilesMenu();
    } catch ( ... ) {
        LOG_ERROR << "Can't open file " << fileName.toStdString();
        return false;
    }

    LOG_DEBUG << "Success loading file " << fileName.toStdString();
    return true;
}

// Strips the passed filename from its directory part.
QString MainWindow::strippedName( const QString& fullFileName ) const
{
    return QFileInfo( fullFileName ).fileName();
}

// Return the currently active CrawlerWidget, or NULL if none
CrawlerWidget* MainWindow::currentCrawlerWidget() const
{
    auto current = qobject_cast<CrawlerWidget*>( mainTabWidget_.currentWidget() );

    return current;
}

// Update the title bar.
void MainWindow::updateTitleBar( const QString& file_name )
{
    QString shownName = tr( "Untitled" );
    if ( !file_name.isEmpty() ) {
        shownName = strippedName( file_name );
    }

    QString indexPart = "";
    if ( session_.windowIndex() > 0 ) {
        indexPart = QString( " #%1" ).arg( session_.windowIndex() + 1 );
    }

    setWindowTitle( tr( "%1 - %2%3" ).arg( shownName, tr( "klogg" ), indexPart ) + tr( " (build " )
                    + kloggUiVersion() + ")" );
}

void MainWindow::addRecentFile( const QString& fileName )
{
    klogg::ui::recentFiles().addRecent( fileName.toStdString() );
    klogg::ui::saveRecentFiles();
    updateRecentFileActions();
}

// Updates the actions for the recent files.
// Must be called after having added a new name to the list.
void MainWindow::updateRecentFileActions()
{
    auto& recentFilesCollection = klogg::ui::recentFiles();
    const auto recent_files = recentFilesCollection.recentFiles();

    const auto recent_files_max_items
        = std::min( static_cast<int>( recent_files.size() ),
                    recentFilesCollection.filesHistoryMaxItems() );

    if ( !recent_files.empty() ) {
        recentFilesMenu->setEnabled( true );
        for ( auto j = 0; j < MAX_RECENT_FILES; ++j ) {
            const auto actionIndex = static_cast<size_t>( j );
            if ( j < recent_files_max_items ) {
                int key = j + ( ( j < 9 ) ? 0x31 : ( 0x61 - 9 ) ); // shortcuts: 1..9 next a,b...
                const auto recentFile = QString::fromStdString( recent_files[ actionIndex ] );
                QString text
                    = tr( "&%1 %2" ).arg( QChar( key ) ).arg( strippedName( recentFile ) );
                recentFileActions[ actionIndex ]->setText( text );
                recentFileActions[ actionIndex ]->setToolTip( recentFile );
                recentFileActions[ actionIndex ]->setData( recentFile );
                recentFileActions[ actionIndex ]->setVisible( true );
            }
            else {
                recentFileActions[ actionIndex ]->setVisible( false );
            }
        }
    }
    else {
        recentFilesMenu->setEnabled( false );
    }

    // separatorAction->setVisible(!recentFiles.isEmpty());
}

// Clear the list of the recent files
void MainWindow::clearRecentFileActions()
{
    klogg::ui::recentFiles().removeAll();
    klogg::ui::saveRecentFiles();
    updateRecentFileActions();
}
// Update our menu bar to match the settings of the crawler
// (used when the tab is changed)
void MainWindow::updateMenuBarFromDocument( const CrawlerWidget* crawler )
{
    const auto encodingMib = crawler->encodingMib();

    auto encodingActions = encodingGroup->actions();
    auto encodingItem = std::find_if( encodingActions.begin(), encodingActions.end(),
                                      [ &encodingMib ]( const auto& action ) {
                                          return ( !encodingMib && !action->data().isValid() )
                                                 || ( encodingMib && action->data().isValid()
                                                      && *encodingMib == action->data().toInt() );
                                      } );

    if ( encodingItem != encodingActions.end() ) {
        ( *encodingItem )->setChecked( true );
    }

    followAction->setChecked( crawler->isFollowEnabled() );
    textWrapAction->setChecked( crawler->isTextWrapEnabled() );
    chipModeAction->blockSignals( true );
    chipModeAction->setChecked( globalChipMode_ );
    chipModeAction->blockSignals( false );
}

// Update the top info line from the session
void MainWindow::updateInfoLine()
{
    QLocale defaultLocale;

    // Following should always work as we will only receive enter
    // this slot if there is a crawler connected.
    QString current_file
        = QDir::toNativeSeparators( session_.getFilename( currentCrawlerWidget() ) );

    uint64_t fileSize;
    uint64_t fileNbLine;
    QDateTime lastModified;

    session_.getFileInfo( currentCrawlerWidget(), &fileSize, &fileNbLine, &lastModified );

    infoLine->setText( current_file );
    infoLine->setPath( current_file );
    sizeField->setText( QString::fromStdString( readableSize( fileSize ) ) );
    encodingField->setText( currentCrawlerWidget()->encodingText() );

    if ( lastModified.isValid() ) {
        const QString date = defaultLocale.toString( lastModified, QLocale::NarrowFormat );
        dateField->setText( tr( "modified on %1" ).arg( date ) );
        dateField->show();
    }
    else {
        dateField->hide();
    }
}

void MainWindow::updateOpenedFilesMenu()
{
    openedFilesMenu->clear();

    const auto& files = session_.openedFiles();

    openedFilesMenu->setEnabled( !files.empty() );

    openedFilesMenu->addAction( selectOpenFileAction );
    openedFilesMenu->addSeparator();

    for ( const auto& file : files ) {
        const auto displayFile = DisplayFilePath{ file };
        auto action = openedFilesMenu->addAction( displayFile.displayName() );

        action->setActionGroup( openedFilesGroup );
        action->setToolTip( displayFile.nativeFullPath() );
        action->setData( displayFile.fullPath() );
    }

    selectOpenFileAction->setDisabled( files.empty() );
}

void MainWindow::updateHighlightersMenu()
{
    highlightersMenu->clearHighlightersMenu();
    highlightersMenu->createHighlightersMenu();
    highlightersMenu->addAction( editHighlightersAction, true );
    highlightersMenu->populateHighlightersMenu();
}

void MainWindow::updateFavoritesMenu()
{
    favoritesMenu->clear();

    favoritesMenu->addAction( addToFavoritesMenuAction );
    favoritesMenu->addAction( removeFromFavoritesAction );

    addToFavoritesMenuAction->setIcon( iconLoader_.load( "icons8-star" ) );

    using namespace klogg::mainwindow;

    addToFavoritesAction->setText(
        QApplication::translate( "klogg::mainwindow::action", action::addToFavoritesText ) );
    addToFavoritesAction->setIcon( iconLoader_.load( "icons8-star" ) );
    addToFavoritesAction->setData( true );

    const auto& favorites = FavoriteFiles::getSynced().favorites();
    auto crawler = currentCrawlerWidget();

    addToFavoritesAction->setEnabled( crawler != nullptr );
    addToFavoritesMenuAction->setEnabled( crawler != nullptr );
    removeFromFavoritesAction->setEnabled( !favorites.empty() );

    if ( crawler ) {
        const auto path = session_.getFilename( crawler );
        if ( std::any_of( favorites.begin(), favorites.end(), FullPathComparator( path ) ) ) {

            addToFavoritesAction->setText( QApplication::translate(
                "klogg::mainwindow::action", action::removeFromFavoritesText ) );
            addToFavoritesAction->setIcon( iconLoader_.load( "icons8-star-filled" ) );
            addToFavoritesAction->setData( false );

            addToFavoritesMenuAction->setEnabled( false );
            addToFavoritesMenuAction->setIcon( iconLoader_.load( "icons8-star-filled" ) );
        }
    }

    favoritesMenu->addSeparator();

    for ( const auto& file : favorites ) {
        auto action = favoritesMenu->addAction( file.displayName() );

        action->setActionGroup( favoritesGroup );
        action->setToolTip( file.nativeFullPath() );
        action->setData( file.fullPath() );
    }
}

void MainWindow::addToFavorites()
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::addToFavorites" );
    if ( const auto crawler = currentCrawlerWidget() ) {
        auto& favorites = FavoriteFiles::get();
        const auto path = session_.getFilename( crawler );

        if ( addToFavoritesAction->data().toBool() ) {
            favorites.add( path );
        }
        else {
            favorites.remove( path );
        }

        favorites.save();

        updateFavoritesMenu();
    }
}

void MainWindow::removeFromFavorites()
{
    KLOGG_UI_TRACE_INSTANT( "MainWindow::removeFromFavorites" );
    const auto& favoriteFiles = FavoriteFiles::get();
    const auto& favorites = favoriteFiles.favorites();
    QStringList files;
    std::transform( favorites.cbegin(), favorites.cend(), std::back_inserter( files ),
                    []( const auto& f ) { return f.nativeFullPath(); } );

    auto currentIndex = 0;

    if ( const auto crawler = currentCrawlerWidget() ) {
        const auto currentPath = session_.getFilename( crawler );
        const auto currentItem
            = std::find_if( favorites.begin(), favorites.end(), FullPathComparator( currentPath ) );
        if ( currentItem != favorites.end() ) {
            currentIndex = static_cast<int>( std::distance( favorites.begin(), currentItem ) );
        }
    }

    bool ok = false;
    const auto pathToRemove = QInputDialog::getItem( this, tr( "Remove from favorites" ),
                                                     tr( "Select item to remove from favorites" ),
                                                     files, currentIndex, false, &ok );
    if ( ok ) {
        removeFromFavorites( pathToRemove );
    }
}

void MainWindow::removeFromFavorites( const QString& pathToRemove )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::removeFromFavorites" );
    auto& favoriteFiles = FavoriteFiles::get();
    const auto& favorites = favoriteFiles.favorites();
    const auto selectedFile = std::find_if( favorites.begin(), favorites.end(),
                                            [ pathToRemove ]( const DisplayFilePath& f ) {
                                                return f.nativeFullPath() == pathToRemove;
                                            } );

    if ( selectedFile != favorites.end() ) {
        favoriteFiles.remove( selectedFile->fullPath() );
        favoriteFiles.save();
        updateFavoritesMenu();
    }
}

void MainWindow::removeFromRecent( const QString& pathToRemove )
{
    KLOGG_UI_TRACE_SCOPE( "MainWindow::removeFromRecent" );
    klogg::ui::recentFiles().removeRecent( pathToRemove.toStdString() );
    klogg::ui::saveRecentFiles();
    updateRecentFileActions();
}

void MainWindow::selectOpenedFile()
{
    KLOGG_UI_TRACE_INSTANT( "MainWindow::selectOpenedFile" );
    auto openedFilesPaths = session_.openedFiles();
    std::vector<DisplayFilePath> openedFiles;
    openedFiles.reserve( openedFilesPaths.size() );
    std::transform( openedFilesPaths.cbegin(), openedFilesPaths.cend(),
                    std::back_inserter( openedFiles ),
                    []( const auto& path ) { return DisplayFilePath{ path }; } );

    QStringList filesToShow;
    std::transform( openedFiles.cbegin(), openedFiles.cend(), std::back_inserter( filesToShow ),
                    []( const auto& f ) { return f.nativeFullPath(); } );

    auto selectFileDialog = std::make_unique<QDialog>( this );
    selectFileDialog->setWindowTitle( tr( "klogg -- switch to file" ) );
    selectFileDialog->setMinimumWidth( 800 );
    selectFileDialog->setMinimumHeight( 600 );

    auto filesModel = std::make_unique<QStringListModel>( filesToShow, selectFileDialog.get() );
    auto filteredModel = std::make_unique<QSortFilterProxyModel>( selectFileDialog.get() );
    filteredModel->setSourceModel( filesModel.get() );

    auto filesView = std::make_unique<QListView>();
    filesView->setModel( filteredModel.get() );
    filesView->setEditTriggers( QAbstractItemView::NoEditTriggers );
    filesView->setSelectionMode( QAbstractItemView::SingleSelection );

    auto filterEdit = std::make_unique<QLineEdit>();
    auto buttonBox
        = std::make_unique<QDialogButtonBox>( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );

    connect( buttonBox.get(), &QDialogButtonBox::accepted, selectFileDialog.get(),
             &QDialog::accept );
    connect( buttonBox.get(), &QDialogButtonBox::rejected, selectFileDialog.get(),
             &QDialog::reject );

    connect( filterEdit.get(), &QLineEdit::textEdited,
             [ model = filteredModel.get(), view = filesView.get() ]( const QString& filter ) {
                 model->setFilterWildcard( filter );
                 model->invalidate();
                 view->selectionModel()->select( model->index( 0, 0 ),
                                                 QItemSelectionModel::SelectCurrent );
             } );

    dispatchToMainThread( [ edit = filterEdit.get() ]() { edit->setFocus(); } );

    connect( selectFileDialog.get(), &QDialog::finished,
             [ this, openedFiles, dialog = selectFileDialog.get(), model = filteredModel.get(),
               view = filesView.get() ]( auto result ) {
                 dialog->deleteLater();
                 if ( result != QDialog::Accepted || !view->selectionModel()->hasSelection() ) {
                     return;
                 }
                 const auto& selectedPath
                     = model->data( view->selectionModel()->selectedIndexes().front() ).toString();
                 const auto selectedFile
                     = std::find_if( openedFiles.begin(), openedFiles.end(),
                                     [ selectedPath ]( const DisplayFilePath& f ) {
                                         return f.nativeFullPath() == selectedPath;
                                     } );

                 if ( selectedFile != openedFiles.end() ) {
                     loadFile( selectedFile->fullPath() );
                 }
             } );

    auto layout = std::make_unique<QVBoxLayout>();
    layout->addWidget( filesView.release() );
    layout->addWidget( filterEdit.release() );
    layout->addWidget( buttonBox.release() );

    selectFileDialog->setLayout( layout.release() );
    selectFileDialog->setModal( true );
    selectFileDialog->open();

    filesModel.release();
    filteredModel.release();
    selectFileDialog.release();
}

void MainWindow::showInfoLabels( bool show )
{
    for ( auto separator : infoToolbarSeparators ) {
        separator->setVisible( show );
    }
    if ( !show ) {
        sizeField->clear();
        dateField->clear();
        encodingField->clear();
        lineNbField->clear();
    }
}

// Write settings to permanent storage
void MainWindow::writeSettings()
{
    // Save the session
    // Generate the ordered list of widgets and their topLine
    std::vector<
        std::tuple<const ViewInterface*, uint64_t, std::shared_ptr<const ViewContextInterface>>>
        widget_list;
    for ( int i = 0; i < mainTabWidget_.count(); ++i ) {
        auto view = qobject_cast<const CrawlerWidget*>( mainTabWidget_.widget( i ) );
        widget_list.emplace_back( view, 0UL, view->context() );
    }
    session_.save( widget_list, saveGeometry() );
}

// Read settings from permanent storage
void MainWindow::readSettings()
{
    // Get and restore the session
    // auto& session = SessionInfo::getSynced();
    /*
     * FIXME: should be in the session
    crawlerWidget->restoreState( session.crawlerState() );
    */

    // History of recent files
    updateRecentFileActions();

    FavoriteFiles::getSynced();
    updateFavoritesMenu();

    // Initial build of the UI-side compiled view of the active sets
    klogg::ui::HighlighterCache::instance().refresh();
    updateHighlightersMenu();
}

void MainWindow::displayQuickFindBar( QuickFindMux::QFDirection direction )
{
    LOG_DEBUG << "MainWindow::displayQuickFindBar";

    // Warn crawlers so they can save the position of the focus in order
    // to do incremental search in the right view.
    Q_EMIT enteringQuickFind();

    const auto crawler = currentCrawlerWidget();
    if ( crawler != nullptr && crawler->isPartialSelection() ) {
        auto selection = crawler->getSelectedText();
        if ( !selection.isEmpty() ) {
            quickFindWidget_.changeDisplayedPattern( selection, false );
        }
    }

    quickFindMux_.setDirection( direction );
    quickFindWidget_.userActivate();
}

void MainWindow::logScreenInfo( QScreen* screen )
{
    LOG_INFO << "screen changed for " << session_.windowIndex();
    if ( screen == nullptr ) {
        return;
    }

    LOG_INFO << "screen name " << screen->name();
    LOG_INFO << "screen size " << screen->size().width() << "x" << screen->size().height();
    LOG_INFO << "screen ratio " << screen->devicePixelRatio();
    LOG_INFO << "screen logical dpi " << screen->logicalDotsPerInch();
    LOG_INFO << "screen physical dpi " << screen->physicalDotsPerInch();
}

void MainWindow::generateDump()
{
    const auto userAction = QMessageBox::warning(
        this, tr( "klogg - generate crash dump" ),
        tr( "This will shutdown klogg and generate diagnostic crash dump. Continue?" ),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No );

    if ( userAction == QMessageBox::Yes ) {
        throw std::logic_error( "test dump" );
    }
}

void MainWindow::updateRecordActionState()
{
    using namespace klogg::mainwindow;

    auto crawler = currentCrawlerWidget();
    if ( !crawler ) {
        recordAction->setEnabled( false );
        recorderManager_.setBoundCommandName( {} );
        return;
    }

    if ( recorderManager_.isRecording() ) {
        recordAction->setText( tr( "Stop: %1" ).arg(
            QFileInfo( recordingFileName_ ).fileName() ) );
        recordAction->setStatusTip( tr( action::stopRecordStatusTip ) );
        recordAction->setIcon( style()->standardIcon( QStyle::SP_MediaStop ) );
        recordAction->setChecked( true );
        recordAction->setEnabled( true );
        return;
    }

    recordAction->setChecked( false );

    const auto fileName = QDir::cleanPath( session_.getFilename( crawler ) );
    auto boundCommand = recorderBindings_.value( fileName );

    LOG_INFO << "updateRecordActionState: file=" << fileName
             << " inMap=" << ( !boundCommand.isEmpty() );

    if ( boundCommand.isEmpty() ) {
        boundCommand = Configuration::get().recorderFileBindings().value( fileName );
        LOG_INFO << "updateRecordActionState: from Configuration="
                 << ( !boundCommand.isEmpty() );
        if ( !boundCommand.isEmpty() ) {
            recorderBindings_[ fileName ] = boundCommand;
        }
    }

    recorderManager_.setBoundCommandName( boundCommand );

    if ( !boundCommand.isEmpty() ) {
        auto& config = Configuration::get();
        const auto commands = config.recorderCommands();
        const auto exists = std::any_of( commands.begin(), commands.end(),
                                         [ &boundCommand ]( const auto& cmd ) {
                                             return cmd.name == boundCommand;
                                         } );
        if ( !exists ) {
            recorderBindings_.remove( fileName );
            recorderManager_.setBoundCommandName( {} );
            boundCommand.clear();

            config.removeRecorderFileBinding( fileName );
            config.save();
        }
    }

    if ( boundCommand.isEmpty() ) {
        recordAction->setText( tr( action::recordText ) );
        recordAction->setStatusTip( tr( action::recordStatusTip ) );
        recordAction->setIcon( iconLoader_.load( "icons8-plus" ) );
    }
    else {
        recordAction->setText( tr( "Record: %1" ).arg( boundCommand ) );
        recordAction->setStatusTip( tr( "Record log output using %1" ).arg( boundCommand ) );
        recordAction->setIcon( style()->standardIcon( QStyle::SP_MediaPlay ) );
    }

    recordAction->setEnabled( true );
}

void MainWindow::showRecordCommandMenu()
{
    const auto& config = Configuration::get();
    const auto commands = config.recorderCommands();

    QMenu menu;
    for ( const auto& cmd : commands ) {
        auto action = menu.addAction( cmd.name );
        connect( action, &QAction::triggered, this,
                 [ this, name = cmd.name ]() {
                     bindRecorder( name );
                     updateRecordActionState();
                 } );
    }

    if ( !commands.isEmpty() ) {
        menu.addSeparator();
    }

    auto manageAction = menu.addAction(
        tr( klogg::mainwindow::action::manageRecorderCommandsText ) );
    connect( manageAction, &QAction::triggered, this, [ this ]() { options(); } );

    auto button = qobject_cast<QToolButton*>(
        toolBar->widgetForAction( recordAction ) );
    if ( button ) {
        menu.exec( button->mapToGlobal( QPoint( 0, button->height() ) ) );
    }
    else {
        menu.exec( QCursor::pos() );
    }
}

void MainWindow::bindRecorder( const QString& commandName )
{
    auto crawler = currentCrawlerWidget();
    if ( !crawler ) {
        return;
    }

    const auto fileName = QDir::cleanPath( session_.getFilename( crawler ) );

    recorderBindings_[ fileName ] = commandName;

    auto& config = Configuration::get();
    config.setRecorderFileBinding( fileName, commandName );
    config.save();
}

void MainWindow::unbindRecorder()
{
    if ( recorderManager_.isRecording() ) {
        return;
    }

    auto crawler = currentCrawlerWidget();
    if ( !crawler ) {
        return;
    }

    const auto fileName = QDir::cleanPath( session_.getFilename( crawler ) );

    recorderBindings_.remove( fileName );

    auto& config = Configuration::get();
    config.removeRecorderFileBinding( fileName );
    config.save();
    updateRecordActionState();
}
