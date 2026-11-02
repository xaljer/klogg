/*
 * Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014, 2015 Nicolas Bonnefon and other contributors
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

// This file implements the CrawlerWidget class.
// It is responsible for creating and managing the two views and all
// the UI elements.  It implements the connection between the UI elements.
// It also interacts with the sets of data (full and filtered).

#include "abstractlogview.h"
#include "active_screen.h"
#include "linetypes_qt.h"
#include "log.h"
#include "log_qt.h"
#include "logfiltereddata_qt.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <mutex>
#include <set>

#include <QAction>
#include <QApplication>
#include <QCompleter>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLineEdit>
#include <QListView>
#include <QPointer>
#include <QScrollBar>
#include <QShortcut>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QTextCodec>
#include <QtConcurrent>
#include <qglobal.h>
#include <qobject.h>
#include <string>

#include "regularexpression.h"

#include "crawlerwidget.h"

#include "configuration.h"
#include "dispatch_to.h"
#include "eventvisualization.h"
#include "filewatcher.h"
#include "fontutils.h"
#include "infoline.h"
#include "quickfindpattern.h"
#include "shortcuts.h"

#include "klogg/api.h"

#include "apievents.h"
#include "apisession.h"
#include "uitracing.h"
#include "uiuserdata.h"

// Palette for error signaling (yellow background)
const QPalette CrawlerWidget::ErrorPalette( Qt::darkYellow );

namespace {

QStringList searchHistoryItems()
{
    QStringList items;
    const auto history = klogg::ui::searchHistory().recentSearches();
    items.reserve( static_cast<qsizetype>( history.size() ) );
    for ( const auto& item : history ) {
        items.append( QString::fromStdString( item ) );
    }
    return items;
}

// Extraction events fire on the session worker thread through a single
// process-wide subscription (the API has no per-extraction callback); they
// are routed to the crawler that registered the extraction id and marshalled
// to the GUI thread
std::mutex g_extractionRoutesMutex;
QHash<quint64, QPointer<CrawlerWidget>> g_extractionRoutes;
std::once_flag g_extractionSubscribeOnce;

void ensureExtractionEventSubscription()
{
    std::call_once( g_extractionSubscribeOnce, [] {
        klogg::ui::apiSession().subscribe( []( const std::string& type, const std::string& json ) {
            const bool completed = type == "event.extraction.completed";
            if ( !completed && type != "event.extraction.progress" ) {
                return;
            }
            const auto document = QJsonDocument::fromJson( QByteArray::fromStdString( json ) );
            if ( !document.isObject() ) {
                return;
            }
            const auto payload = document.object();
            const auto extractionId
                = static_cast<quint64>( payload[ QStringLiteral( "extractionId" ) ].toDouble() );
            QPointer<CrawlerWidget> crawler;
            {
                std::lock_guard lock( g_extractionRoutesMutex );
                crawler = g_extractionRoutes.value( extractionId );
            }
            if ( crawler.isNull() ) {
                return;
            }
            const auto count
                = static_cast<uint32_t>( payload[ completed ? QStringLiteral( "eventCount" )
                                                            : QStringLiteral( "eventsFound" ) ]
                                             .toDouble() );
            const double progress = payload[ QStringLiteral( "progress" ) ].toDouble();
            QMetaObject::invokeMethod(
                crawler,
                [ crawler, extractionId, completed, progress, count ]() {
                    if ( crawler.isNull() ) {
                        return;
                    }
                    if ( completed ) {
                        crawler->handleEventExtractionCompleted( extractionId, count );
                    }
                    else {
                        crawler->handleEventExtractionProgress( extractionId, progress, count );
                    }
                },
                Qt::QueuedConnection );
        } );
    } );
}

// File lifecycle events (indexing progress/completion, on-disk changes)
// fire on the session worker thread through a single process-wide
// subscription; they are routed to every crawler showing the file (several
// views may share one file entry) and marshalled to the GUI thread
std::mutex g_fileRoutesMutex;
QHash<klogg::api::FileId, QList<QPointer<CrawlerWidget>>> g_fileRoutes;
std::once_flag g_fileSubscribeOnce;

LoadingStatus loadingStatusFromEventReason( const QString& reason )
{
    if ( reason == QLatin1String( "interrupted" ) ) {
        return LoadingStatus::Interrupted;
    }
    // The engine reports no other non-successful indexing status
    return LoadingStatus::NoMemory;
}

void ensureFileEventSubscription()
{
    std::call_once( g_fileSubscribeOnce, [] {
        klogg::ui::apiSession().subscribe( []( const std::string& type, const std::string& json ) {
            const bool progress = type == "indexing.progress";
            const bool indexed = type == "file.indexed" || type == "file.reindexed";
            const bool failed = type == "file.indexing_failed";
            const bool changed = type == "file.changed";
            const bool marksChanged = type == "marks.changed";
            if ( !progress && !indexed && !failed && !changed && !marksChanged ) {
                return;
            }
            const auto document = QJsonDocument::fromJson( QByteArray::fromStdString( json ) );
            if ( !document.isObject() ) {
                return;
            }
            const auto payload = document.object();
            const auto fileId = static_cast<klogg::api::FileId>(
                payload[ QStringLiteral( "fileId" ) ].toDouble() );
            QList<QPointer<CrawlerWidget>> crawlers;
            {
                std::lock_guard lock( g_fileRoutesMutex );
                crawlers = g_fileRoutes.value( fileId );
            }
            if ( crawlers.isEmpty() ) {
                return;
            }

            const int progressValue
                = static_cast<int>( payload[ QStringLiteral( "progress" ) ].toDouble() );
            const auto reason = payload[ QStringLiteral( "reason" ) ].toString();
            const auto loadingStatus = loadingStatusFromEventReason( reason );
            // The API "modified" reason maps to the native DataAdded
            const auto fileStatus = reason == QLatin1String( "truncated" )
                                        ? MonitoredFileStatus::Truncated
                                        : MonitoredFileStatus::DataAdded;

            for ( const auto& crawler : crawlers ) {
                if ( crawler.isNull() ) {
                    continue;
                }
                QMetaObject::invokeMethod(
                    crawler,
                    [ crawler, progress, progressValue, indexed, failed, changed, marksChanged,
                      loadingStatus, fileStatus ]() {
                        if ( crawler.isNull() ) {
                            return;
                        }
                        if ( progress ) {
                            crawler->handleIndexingProgress( progressValue );
                        }
                        else if ( failed ) {
                            crawler->handleIndexingFinished( loadingStatus );
                        }
                        else if ( indexed ) {
                            crawler->handleIndexingFinished( LoadingStatus::Successful );
                        }
                        else if ( changed ) {
                            crawler->handleFileChanged( fileStatus );
                        }
                        else if ( marksChanged ) {
                            crawler->handleMarksChanged();
                        }
                    },
                    Qt::QueuedConnection );
            }
        } );
    } );
}

} // namespace

// Implementation of the view context for the CrawlerWidget
class CrawlerWidgetContext : public ViewContextInterface {
public:
    // Construct from the stored string representation
    explicit CrawlerWidgetContext( const QString& string );
    // Construct from the value passsed
    CrawlerWidgetContext( QList<int> sizes, bool ignoreCase, bool autoRefresh, bool followFile,
                          bool useRegexp, bool inverseRegexp, bool useBooleanCombination,
                          QList<LineNumber> markedLines )
        : sizes_( sizes )
        , ignoreCase_( ignoreCase )
        , autoRefresh_( autoRefresh )
        , followFile_( followFile )
        , useRegexp_( useRegexp )
        , inverseRegexp_( inverseRegexp )
        , useBooleanCombination_( useBooleanCombination )
    {
        std::transform( markedLines.cbegin(), markedLines.cend(), std::back_inserter( marks_ ),
                        []( const auto& m ) { return m.get(); } );
    }

    // Implementation of the ViewContextInterface function
    QString toString() const override;

    // Access the Qt sizes array for the QSplitter
    QList<int> sizes() const
    {
        return sizes_;
    }

    bool ignoreCase() const
    {
        return ignoreCase_;
    }
    bool autoRefresh() const
    {
        return autoRefresh_;
    }
    bool followFile() const
    {
        return followFile_;
    }
    bool useRegexp() const
    {
        return useRegexp_;
    }
    bool inverseRegexp() const
    {
        return inverseRegexp_;
    }
    bool useBooleanCombination() const
    {
        return useBooleanCombination_;
    }

    QList<LineNumber::UnderlyingType> marks() const
    {
        return marks_;
    }

private:
    void loadFromString( const QString& string );
    void loadFromJson( const QString& json );

private:
    QList<int> sizes_;

    bool ignoreCase_;
    bool autoRefresh_;
    bool followFile_;
    bool useRegexp_;
    bool inverseRegexp_;
    bool useBooleanCombination_;

    QList<LineNumber::UnderlyingType> marks_;
};

// Constructor only does trivial construction. The real work is done once
// the data is attached.
CrawlerWidget::CrawlerWidget( QWidget* parent )
    : QSplitter( parent )
    , iconLoader_{ this }
{
}

CrawlerWidget::~CrawlerWidget()
{
    // The creator cancels its searches: a SearchId outliving its widget
    // would keep the result set allocated in the API session until the file
    // is closed
    for ( auto& [ view, viewData ] : filteredViewsData_ ) {
        Q_UNUSED( view );
        if ( viewData.searchId ) {
            klogg::ui::apiSession().cancelSearch( *viewData.searchId );
        }
    }
    unregisterFileRoute();
    invalidateEventExtraction();
}

CrawlerWidget::FilteredViewData& CrawlerWidget::currentViewData()
{
    return filteredViewsData_.at( filteredView_ );
}

void CrawlerWidget::cancelViewSearch( FilteredViewData& viewData )
{
    if ( viewData.searchId ) {
        klogg::ui::apiSession().cancelSearch( *viewData.searchId );
        viewData.searchId.reset();
    }
}

void CrawlerWidget::interruptViewSearch( FilteredViewData& viewData )
{
    // Stops the search like cancelViewSearch but keeps the result set so the
    // tab keeps rendering the matches found so far (the search id stays
    // valid; only a later cancel drops it)
    if ( viewData.searchId ) {
        klogg::ui::apiSession().interruptSearch( *viewData.searchId );
    }
}

void CrawlerWidget::mirrorMarksToCurrentData()
{
    // Mark navigation caches of every filtered tab follow the session marks
    for ( auto& [ view, viewData ] : filteredViewsData_ ) {
        Q_UNUSED( viewData );
        view->invalidateMarksCache();
    }

    rebuildEmptyProjection();

    // A live search keeps its own engine-side union; the API mirrors session
    // marks into it on each enabling switch, so re-push the visibility to
    // pick up marks added after the search started
    if ( currentSearchId_ ) {
        const auto* visibilityItem = visibilityModel_->item( visibilityBox_->currentIndex() );
        const auto visibility = visibilityItem->data().value<FilteredView::Visibility>();
        if ( visibility.testFlag( LogFilteredData::VisibilityFlags::Marks ) ) {
            filteredView_->setVisibility( visibility );
        }
    }
}

void CrawlerWidget::rebuildEmptyProjection()
{
    // The combo state governs which classes join the search-less projection,
    // same as the empty data set's visibility did
    const auto* visibilityItem = visibilityModel_->item( visibilityBox_->currentIndex() );
    const auto visibility = visibilityItem->data().value<FilteredView::Visibility>();
    using VisibilityFlags = LogFilteredData::VisibilityFlags;

    std::vector<LineNumber> lines;
    if ( visibility.testFlag( VisibilityFlags::Marks ) ) {
        const auto marks = klogg::ui::apiSession().listMarks( fileId_ );
        if ( marks ) {
            lines.assign( marks->begin(), marks->end() );
        }
        else {
            LOG_WARNING << "CrawlerWidget: listMarks failed: " << marks.error().message;
        }
    }
    if ( visibility.testFlag( VisibilityFlags::Events ) ) {
        // The tooltip map keys exactly the populated extraction's
        // category-filtered event lines
        lines.reserve( lines.size() + static_cast<std::size_t>( eventTooltipsByLine_.size() ) );
        for ( auto it = eventTooltipsByLine_.cbegin(); it != eventTooltipsByLine_.cend(); ++it ) {
            lines.push_back( it.key() );
        }
    }

    std::sort( lines.begin(), lines.end() );
    lines.erase( std::unique( lines.begin(), lines.end() ), lines.end() );
    emptyProjectionLines_ = std::move( lines );

    overview_.setEmptyProjection( emptyProjectionLines_ );
    for ( auto& [ view, viewData ] : filteredViewsData_ ) {
        Q_UNUSED( viewData );
        view->setEmptyProjection( emptyProjectionLines_ );
    }
}

void CrawlerWidget::adoptSearchData( klogg::api::SearchId searchId )
{
    auto& viewData = currentViewData();
    viewData.searchId = searchId;

    currentSearchId_ = searchId;

    filteredView_->useNewFilteredData( searchId );
    logMainView_->useNewFiltering( searchId );

    // Mirror the session-level marks into the new render cache before the
    // visibility (which may include Marks) is applied
    mirrorMarksToCurrentData();

    // Apply the visibility combo to the new set; a new search's set has no
    // Events array, so an active Events visibility re-applies the current
    // extraction (or starts one) into it
    QStandardItem* visibilityItem = visibilityModel_->item( visibilityBox_->currentIndex() );
    const auto visibility = visibilityItem->data().value<FilteredView::Visibility>();
    filteredView_->setVisibility( visibility );
    if ( visibility.testFlag( LogFilteredData::VisibilityFlags::Events ) ) {
        performEventSearch();
    }

    // Search progress fires on the API worker thread: marshal it back to the
    // GUI thread and ignore stale ids (a replaced search may still deliver)
    const auto searchStartLine = searchStartLine_;
    klogg::ui::apiSession().onSearchProgress( searchId, [ this, searchId, searchStartLine ](
                                                            klogg::api::SearchId,
                                                            LinesCount matched, float ratio ) {
        QPointer<CrawlerWidget> guard( this );
        QMetaObject::invokeMethod(
            this,
            [ guard, searchId, matched, ratio, searchStartLine ]() {
                if ( !guard || guard->currentSearchId_ != searchId ) {
                    return;
                }
                guard->updateMatchesFilteredView(
                    matched, static_cast<int>( std::lround( ratio * 100.0f ) ), searchStartLine );
            },
            Qt::QueuedConnection );
    } );

    // Recompute the content of the filtered window and the match overview
    filteredView_->updateData();
    overview_.updateData( cachedTotalLines_ );
    update();

    // A fast search may have completed before the progress callback was
    // registered; deliver its final state so the UI leaves progress mode
    const auto progress = klogg::ui::apiSession().searchProgress( searchId );
    if ( progress && progress->progress >= 1.0f ) {
        updateMatchesFilteredView( progress->total_matches, 100, searchStartLine_ );
    }
}

void CrawlerWidget::switchToEmptyData()
{
    currentSearchId_.reset();

    filteredView_->useNewFilteredData( std::nullopt );
    logMainView_->useNewFiltering( std::nullopt );
    mirrorMarksToCurrentData();

    filteredView_->updateData();
    overview_.updateData( cachedTotalLines_ );
    update();
}

// The top line is first one on the main display
LineNumber CrawlerWidget::getTopLine() const
{
    return logMainView_->getTopLine();
}

QString CrawlerWidget::getSelectedText() const
{
    if ( filteredView_->hasFocus() )
        return filteredView_->getSelectedText();
    else
        return logMainView_->getSelectedText();
}

void CrawlerWidget::copyWithColor() const
{
    if ( filteredView_->hasFocus() )
        filteredView_->copyWithColor();
    else
        logMainView_->copyWithColor();
}

bool CrawlerWidget::isPartialSelection() const
{
    if ( filteredView_->hasFocus() )
        return filteredView_->isPartialSelection();
    else
        return logMainView_->isPartialSelection();
}

void CrawlerWidget::selectAll()
{
    activeView()->selectAll();
}

std::optional<int> CrawlerWidget::encodingMib() const
{
    return encodingMib_;
}

bool CrawlerWidget::isFollowEnabled() const
{
    return logMainView_->isFollowEnabled();
}

bool CrawlerWidget::isTextWrapEnabled() const
{
    return logMainView_->isTextWrapEnabled();
}

void CrawlerWidget::reloadPredefinedFilters() const
{
    predefinedFilters_->populatePredefinedFilters();
}

QString CrawlerWidget::encodingText() const
{
    return encodingText_;
}

// Return a pointer to the view in which we should do the QuickFind
SearchableWidgetInterface* CrawlerWidget::doGetActiveSearchable() const
{
    return activeView();
}

// Return all the searchable widgets (views)
std::vector<QObject*> CrawlerWidget::doGetAllSearchables() const
{
    std::vector<QObject*> searchables = { logMainView_, filteredView_ };

    return searchables;
}

// Update the state of the parent
void CrawlerWidget::doSendAllStateSignals()
{
    Q_EMIT newSelection( currentLineNumber_, 0_lcount, 0_lcol, 0_length );
    if ( !loadingInProgress_ )
        Q_EMIT loadingFinished( LoadingStatus::Successful );
}

void CrawlerWidget::changeEvent( QEvent* event )
{
    if ( event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange ) {
        dispatchToMainThread( [ this ] {
            loadIcons();
            applyToggleButtonPaletteStyle();
            searchInfoLineDefaultPalette_ = this->palette();
        } );
    }

    QWidget::changeEvent( event );
}

//
// Public Q_SLOTS:
//

void CrawlerWidget::stopLoading()
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::stopLoading" );
    if ( currentSearchId_ ) {
        klogg::ui::apiSession().cancelSearch( *currentSearchId_ );
    }
    // Stop cancels a running extraction; a completed one keeps its results
    if ( eventExtractionId_ && populatedExtractionId_ != eventExtractionId_ ) {
        invalidateEventExtraction();
    }
    const auto interrupted = klogg::ui::apiSession().interruptLoading( fileId_ );
    if ( !interrupted ) {
        LOG_WARNING << "interruptLoading failed: " << interrupted.error().message;
    }
}

void CrawlerWidget::reload()
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::reload" );
    searchState_.resetState();

    auto& viewData = currentViewData();
    cancelViewSearch( viewData );
    invalidateEventExtraction();
    clearSliceRangeIndex();

    const auto marks = klogg::ui::apiSession().listMarks( fileId_ );
    if ( marks && !marks->empty() ) {
        klogg::ui::apiSession().removeMarks( fileId_, *marks );
    }

    switchToEmptyData();
    printSearchInfoMessage();

    // Triggered on the API worker and returns right away; progress and the
    // completion keep flowing through the session events, so the GUI stays
    // responsive and the reload can be interrupted (Stop)
    const auto triggered = klogg::ui::apiSession().reloadFileAsync( fileId_, {} );
    if ( !triggered ) {
        LOG_WARNING << "reloadFileAsync failed: " << triggered.error().message;
    }

    // A reload is considered as a first load,
    // this is to prevent the "new data" icon to be triggered.
    firstLoadDone_ = false;
}

void CrawlerWidget::setEncoding( std::optional<int> mib )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::setEncoding" );
    encodingMib_ = std::move( mib );
    updateEncoding();

    update();
}

void CrawlerWidget::focusSearchEdit()
{
    if ( patternInputWidget_->isChipMode() ) {
        patternInputWidget_->focusInput( Qt::ShortcutFocusReason );
    }
    else {
        searchLineEdit_->setFocus( Qt::ShortcutFocusReason );
    }
}

void CrawlerWidget::goToLine()
{
    // Modal line-number dialog, so only the entry is marked
    KLOGG_UI_TRACE_INSTANT( "CrawlerWidget::goToLine" );
    bool isLineSelected = true;
    auto newLine = QInputDialog::getText( this, "Jump to line", "Line number" )
                       .toULongLong( &isLineSelected );

    if ( isLineSelected ) {
        if ( newLine == 0 ) {
            newLine = 1;
        }

        const auto selectedLine
            = LineNumber( static_cast<LineNumber::UnderlyingType>( newLine - 1 ) );
        filteredView_->trySelectLine( filteredIndexFloor( selectedLine ) );
        logMainView_->trySelectLine( selectedLine );
    }
}

void CrawlerWidget::jumpToTop()
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::jumpToTop" );
    logMainView_->selectAndDisplayLine( 0_lnum );
    filteredView_->selectAndDisplayLine( 0_lnum );
}

void CrawlerWidget::scrollToLine( LineNumber line )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::scrollToLine" );
    logMainView_->selectAndDisplayLine( line );
    filteredView_->selectAndDisplayLine( 0_lnum );
}

void CrawlerWidget::setSearchText( const QString& text )
{
    searchLineEdit_->setEditText( text );
    patternInputWidget_->setText( text );
}

//
// Protected functions
//
void CrawlerWidget::doSetData( klogg::api::FileId fileId )
{
    fileId_ = fileId;
    // From here on the indexing lifecycle reaches this widget through the
    // per-file session event route
    ensureFileEventSubscription();
    registerFileRoute();
    refreshTotalLines();
}

void CrawlerWidget::doSetQuickFindPattern( std::shared_ptr<QuickFindPattern> qfp )
{
    quickFindPattern_ = std::move( qfp );

    // We do setup now, assuming doSetData has been called before
    // us, that's not great really...
    setup();
}

void CrawlerWidget::doReplayIndexingFinished()
{
    handleIndexingFinished( LoadingStatus::Successful );
}

void CrawlerWidget::doSetViewContext( const QString& view_context )
{
    LOG_DEBUG << "CrawlerWidget::doSetViewContext: " << view_context.toLocal8Bit().data();

    const auto context = CrawlerWidgetContext{ view_context };

    setSizes( context.sizes() );
    matchCaseButton_->setChecked( !context.ignoreCase() );
    useRegexpButton_->setChecked( context.useRegexp() );
    inverseButton_->setChecked( context.inverseRegexp() );
    booleanButton_->setChecked( context.useBooleanCombination() );

    searchRefreshButton_->setChecked( context.autoRefresh() );
    // Manually call the handler as it is not called when changing the state programmatically
    searchRefreshChangedHandler( context.autoRefresh() );

    const auto& config = Configuration::get();
    logMainView_->followSet( context.followFile() && config.anyFileWatchEnabled() );

    const auto savedMarks = context.marks();
    std::transform( savedMarks.cbegin(), savedMarks.cend(), std::back_inserter( savedMarkedLines_ ),
                    []( const auto& l ) { return LineNumber( l ); } );
}

std::shared_ptr<const ViewContextInterface> CrawlerWidget::doGetViewContext() const
{
    QList<LineNumber> markedLines;
    const auto marks = klogg::ui::apiSession().listMarks( fileId_ );
    if ( marks ) {
        for ( const auto line : *marks ) {
            markedLines.append( line );
        }
    }

    auto context = std::make_shared<const CrawlerWidgetContext>(
        sizes(), ( !matchCaseButton_->isChecked() ), searchRefreshButton_->isChecked(),
        logMainView_->isFollowEnabled(), useRegexpButton_->isChecked(), inverseButton_->isChecked(),
        booleanButton_->isChecked(), markedLines );

    return static_cast<std::shared_ptr<const ViewContextInterface>>( context );
}

//
// Q_SLOTS:
//

void CrawlerWidget::startNewSearch()
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::startNewSearch" );
    if ( keepSearchResultsButton_->isChecked() ) {
        keepSearchResultsButton_->setChecked( false );

        // The current tab keeps its results; stop its ongoing search and
        // start the new tab with the search-less empty projection until its
        // first API search
        interruptViewSearch( currentViewData() );

        filteredView_ = new FilteredView( quickFindPattern_.get(), fileId_ );
        filteredViewsData_[ filteredView_ ] = { std::nullopt };

        currentSearchId_.reset();

        connectAllFilteredViewSlots( filteredView_ );

        auto index = tabbedFilteredView_->addTab( filteredView_, "" );
        tabbedFilteredView_->setCurrentIndex( index );

        logMainView_->useNewFiltering( std::nullopt );
        mirrorMarksToCurrentData();

        applyConfiguration();
    }

    tabbedFilteredView_->setTabText( tabbedFilteredView_->currentIndex(),
                                     "Find \"" + getCurrentSearchText() + "\"" );

    // Record the search line in the recent list
    klogg::ui::searchHistory().addRecent( getCurrentSearchText().toStdString() );
    klogg::ui::saveSearchHistory();

    // Update the SearchLine (history)
    updateSearchCombo();
    // Call the private function to do the search
    replaceCurrentSearch( getCurrentSearchText() );
}

void CrawlerWidget::updatePredefinedFiltersWidget()
{
    predefinedFilters_->updateSearchPattern( getCurrentSearchText(), booleanButton_->isChecked() );
}

void CrawlerWidget::stopSearch()
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::stopSearch" );
    // The tab keeps rendering the matches found so far
    interruptViewSearch( currentViewData() );
    searchState_.stopSearch();
    printSearchInfoMessage();
}

void CrawlerWidget::clearSearchHistory()
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::clearSearchHistory" );
    // Clear line
    searchLineEdit_->clear();

    klogg::ui::searchHistory().clear();
    klogg::ui::saveSearchHistory();

    searchLineCompleter_->setModel( new QStringListModel( {}, searchLineCompleter_ ) );
}

void CrawlerWidget::editSearchHistory()
{
    // Modal input dialog, so only the entry is marked
    KLOGG_UI_TRACE_INSTANT( "CrawlerWidget::editSearchHistory" );

    bool ok;
    QString newHistory
        = QInputDialog::getMultiLineText( this, tr( "klogg" ), tr( "Search history:" ),
                                          searchHistoryItems().join( QChar::LineFeed ), &ok );

    if ( ok ) {
        auto& history = klogg::ui::searchHistory();
        history.clear();
#if QT_VERSION >= QT_VERSION_CHECK( 5, 15, 0 )
        auto items = newHistory.split( QChar::LineFeed, Qt::SkipEmptyParts );
#else
        auto items = newHistory.split( QChar::LineFeed, QString::SkipEmptyParts );
#endif
        std::for_each( items.rbegin(), items.rend(), [ &history ]( const auto& item ) {
            history.addRecent( item.toStdString() );
            LOG_INFO << item;
        } );
        klogg::ui::saveSearchHistory();
    }

    updateSearchCombo();
}

void CrawlerWidget::saveAsPredefinedFilter()
{
    KLOGG_UI_TRACE_INSTANT( "CrawlerWidget::saveAsPredefinedFilter" );
    const auto currentText = getCurrentSearchText();

    Q_EMIT saveCurrentSearchAsPredefinedFilter( currentText, useRegexpButton_->isChecked() );
}

void CrawlerWidget::showSearchContextMenu()
{
    KLOGG_UI_TRACE_INSTANT( "CrawlerWidget::showSearchContextMenu" );
    if ( searchLineContextMenu_ )
        searchLineContextMenu_->exec( QCursor::pos( activeScreen( this ) ) );
}

void CrawlerWidget::updateMatchesFilteredView( LinesCount nbMatches, int progress,
                                               LineNumber initialPosition )
{
    LOG_DEBUG << "updateMatchesFilteredView received.";

    searchInfoLine_->show();

    if ( progress == 100 ) {
        // Searching done
        printSearchInfoMessage( nbMatches );
        searchInfoLine_->hideGauge();
        // De-activate the stop button
        stopButton_->setEnabled( false );
        stopButton_->hide();
        searchButton_->show();
        clearButton_->show();
    }
    else {
        // Search in progress
        // We ignore 0% and 100% to avoid a flash when the search is very short
        if ( progress > 0 ) {
            // Some languages translate the plural the same as the singular, so use the full string
            QString searchType = tr( "Search" );
            searchInfoLine_->setText( tr( "%1 in progress (%2 %)..." )
                                          .arg( searchType )
                                          .arg( QString::number( progress ) )
                                      + ( nbMatches.get() > 1
                                              ? tr( " %1 matches found so far." )
                                                    .arg( QString::number( nbMatches.get() ) )
                                              : tr( " %1 match found so far." )
                                                    .arg( QString::number( nbMatches.get() ) ) ) );

            searchInfoLine_->displayGauge( progress );
        }
    }

    // If more (or less, e.g. come back to 0) matches have been found
    if ( nbMatches != nbMatches_ ) {
        nbMatches_ = nbMatches;

        // Recompute the content of the filtered window.
        filteredView_->updateData();

        // Update the match overview
        overview_.updateData( cachedTotalLines_ );

        // New data found icon
        if ( initialPosition > 0_lnum ) {
            changeDataStatus( DataStatus::NEW_FILTERED_DATA );
        }

        // Also update the top window for the coloured bullets.
        update();
    }

    // Try to restore the filtered window selection close to where it was
    // only for full searches to avoid disconnecting follow mode!
    if ( ( progress == 100 ) && ( initialPosition == searchStartLine_ )
         && ( !isFollowEnabled() ) ) {
        const auto currenLineIndex = filteredIndexFloor( currentLineNumber_ );
        LOG_DEBUG << "updateFilteredView: restoring selection: "
                  << " absolute line number (0based) " << currentLineNumber_ << " index "
                  << currenLineIndex;
        filteredView_->selectAndDisplayLine( currenLineIndex );
        filteredView_->setSearchLimits( searchStartLine_, searchEndLine_ );
    }
}

void CrawlerWidget::updateEventsFilteredView( LinesCount nbEvents, int progress,
                                              LineNumber initialPosition )
{
    LOG_DEBUG << "updateEventsFilteredView received: nbEvents=" << nbEvents.get()
              << " progress=" << progress << " initialPosition=" << initialPosition;

    searchInfoLine_->show();

    if ( progress == 100 ) {
        // Pull the extraction results into the rendered set; the final text
        // is set when the populate applies
        if ( !populatedExtractionId_ || populatedExtractionId_ != eventExtractionId_ ) {
            populateEventsFromExtraction();
        }
        searchInfoLine_->setText( tr( "Found %1 event(s)." ).arg( nbEvents.get() ) );
        searchInfoLine_->hideGauge();

        stopButton_->setEnabled( false );
        stopButton_->hide();
        searchButton_->show();
        clearButton_->show();
    }
    else if ( progress > 0 ) {
        searchInfoLine_->setText(
            tr( "Event search in progress (%1 %)..." ).arg( QString::number( progress ) )
            + ( nbEvents.get() > 1
                    ? tr( " %1 events found so far." ).arg( QString::number( nbEvents.get() ) )
                    : tr( " %1 event found so far." ).arg( QString::number( nbEvents.get() ) ) ) );
        searchInfoLine_->displayGauge( progress );
    }

    // Update filtered projection in case Events visibility is active.
    filteredView_->updateData();
    overview_.updateData( cachedTotalLines_ );
    if ( initialPosition > 0_lnum ) {
        changeDataStatus( DataStatus::NEW_FILTERED_DATA );
    }
    update();
}

void CrawlerWidget::registerEventRoute( klogg::api::ExtractionId extractionId )
{
    std::lock_guard lock( g_extractionRoutesMutex );
    g_extractionRoutes.insert( extractionId, this );
}

void CrawlerWidget::unregisterEventRoute( klogg::api::ExtractionId extractionId )
{
    std::lock_guard lock( g_extractionRoutesMutex );
    g_extractionRoutes.remove( extractionId );
}

void CrawlerWidget::registerFileRoute()
{
    if ( fileId_ == 0 ) {
        return;
    }
    std::lock_guard lock( g_fileRoutesMutex );
    g_fileRoutes[ fileId_ ].append( this );
}

void CrawlerWidget::unregisterFileRoute()
{
    if ( fileId_ == 0 ) {
        return;
    }
    std::lock_guard lock( g_fileRoutesMutex );
    auto it = g_fileRoutes.find( fileId_ );
    if ( it == g_fileRoutes.end() ) {
        return;
    }
    // A null QPointer is a crawler that was destroyed without unregistering
    it->erase( std::remove_if( it->begin(), it->end(),
                               [ this ]( const QPointer<CrawlerWidget>& crawler ) {
                                   return crawler.isNull() || crawler.data() == this;
                               } ),
               it->end() );
    if ( it->empty() ) {
        g_fileRoutes.erase( it );
    }
}

void CrawlerWidget::handleIndexingProgress( int progress )
{
    // Keeps the signal forwarding shape the native LogData connection had;
    // MainWindow consumes it through the signal mux
    Q_EMIT loadingProgressed( progress );
}

void CrawlerWidget::handleIndexingFinished( LoadingStatus status )
{
    loadingFinishedHandler( status );
}

void CrawlerWidget::handleFileChanged( MonitoredFileStatus status )
{
    fileChangedHandler( status );
}

void CrawlerWidget::handleMarksChanged()
{
    // Same invalidation as a local mark edit: drop every tab's navigation
    // cache and rebuild the marks/events projection from the session state
    mirrorMarksToCurrentData();

    // Same repaint set as markLinesFromMain
    filteredView_->updateData();
    logMainView_->updateData();
    overview_.updateData( cachedTotalLines_ );
    update();
}

void CrawlerWidget::invalidateEventExtraction()
{
    // Drop in-flight populate results along with the handle
    ++eventPopulateGeneration_;
    if ( eventExtractionId_ ) {
        unregisterEventRoute( *eventExtractionId_ );
        klogg::ui::apiSession().cancelExtraction( *eventExtractionId_ );
        eventExtractionId_.reset();
    }
    populatedExtractionId_.reset();
    eventTooltipsByLine_.clear();
    rebuildEmptyProjection();
}

void CrawlerWidget::handleEventExtractionProgress( klogg::api::ExtractionId extractionId,
                                                   double progress, uint32_t eventsFound )
{
    if ( !eventExtractionId_ || *eventExtractionId_ != extractionId ) {
        return;
    }
    updateEventsFilteredView( LinesCount( eventsFound ),
                              static_cast<int>( std::lround( progress * 100.0 ) ), 0_lnum );
}

void CrawlerWidget::handleEventExtractionCompleted( klogg::api::ExtractionId extractionId,
                                                    uint32_t eventCount )
{
    if ( !eventExtractionId_ || *eventExtractionId_ != extractionId ) {
        return;
    }
    unregisterEventRoute( extractionId );
    updateEventsFilteredView( LinesCount( eventCount ), 100, searchStartLine_ );
}

void CrawlerWidget::populateEventsFromExtraction()
{
    if ( !eventExtractionId_ ) {
        return;
    }

    const auto extractionId = *eventExtractionId_;
    const auto categories = currentEventCategories_;
    const quint64 generation = ++eventPopulateGeneration_;

    struct EventPopulateResult {
        bool ok = false;
        QList<LineNumber> eventLines;
        QMap<LineNumber, QString> tooltipsByLine;
        QVector<SliceLineRange> sliceRanges;
    };

    // API pages are pulled off the GUI thread; the completion applies them to
    // the crawler-level state (tooltips, overlay, empty projection)
    auto* watcher = new QFutureWatcher<EventPopulateResult>( this );
    connect( watcher, &QFutureWatcher<EventPopulateResult>::finished, this,
             [ this, watcher, extractionId, generation ]() {
                 const auto result = watcher->result();
                 watcher->deleteLater();

                 if ( generation != eventPopulateGeneration_ || !result.ok
                      || extractionId != eventExtractionId_ ) {
                     LOG_WARNING << "event populate result dropped as stale";
                     return;
                 }

                 populatedExtractionId_ = extractionId;

                 // Tooltip text is per-tab state; the views pull it through
                 // their provider, the search-less projection reads the keys
                 eventTooltipsByLine_ = result.tooltipsByLine;
                 rebuildEmptyProjection();

                 sliceRanges_ = result.sliceRanges;
                 sliceStartsByLine_.clear();
                 sliceEndsByLine_.clear();
                 for ( int i = 0; i < sliceRanges_.size(); ++i ) {
                     const auto& range = sliceRanges_[ i ];
                     sliceStartsByLine_[ range.beginLine.get() ].append( i );
                     sliceEndsByLine_[ range.endLine.get() ].append( i );
                 }
                 activeSliceIndicesCache_.clear();
                 activeSliceCacheValid_ = false;

                 searchInfoLine_->setText(
                     tr( "Found %1 event(s)." ).arg( result.eventLines.size() ) );
                 searchInfoLine_->hideGauge();

                 filteredView_->updateData();
                 overview_.updateData( cachedTotalLines_ );
                 changeDataStatus( DataStatus::NEW_FILTERED_DATA );
                 update();

                 if ( selectedSourceLine_.has_value() ) {
                     Q_EMIT selectedSourceLineChanged( *selectedSourceLine_ );
                 }
             } );

    watcher->setFuture( QtConcurrent::run( [ extractionId, categories ]() {
        EventPopulateResult result;
        auto& apiSession = klogg::ui::apiSession();

        constexpr uint32_t eventsPageSize = 5000;
        std::vector<klogg::api::ExtractedEvent> events;
        for ( uint64_t from = 0;; from += eventsPageSize ) {
            const auto page = apiSession.extractedEvents( extractionId, from, eventsPageSize );
            if ( !page ) {
                LOG_WARNING << "populateEventsFromExtraction: extractedEvents failed: "
                            << page.error().message;
                return result;
            }
            events.insert( events.end(), page->begin(), page->end() );
            if ( page->size() < eventsPageSize ) {
                break;
            }
        }

        // First event wins on a shared line, same as the legacy tooltip logic
        std::map<LineNumber, const klogg::api::ExtractedEvent*> eventsByLine;
        for ( const auto& event : events ) {
            eventsByLine.emplace( event.line, &event );
        }

        const auto categorySelected = [ & ]( const QString& category ) {
            return categories.isEmpty() || categories.contains( category );
        };

        for ( const auto& event : events ) {
            const auto category = QString::fromStdString( event.category );
            if ( !categorySelected( category ) ) {
                continue;
            }
            result.eventLines.append( event.line );
            if ( result.tooltipsByLine.contains( event.line ) ) {
                continue;
            }

            const auto name = QString::fromStdString( event.name );
            const auto description = QString::fromStdString( event.description );
            QString tooltip;
            if ( description.isEmpty() ) {
                tooltip = QString( "<b>%1</b> [%2]" )
                              .arg( name )
                              .arg( category.isEmpty() ? "N/A" : category );
            }
            else {
                tooltip = QString( "<b>%1</b> [%2]<br>%3" )
                              .arg( name )
                              .arg( category.isEmpty() ? "N/A" : category )
                              .arg( description );
            }
            result.tooltipsByLine.insert( event.line, tooltip );
        }

        const auto apiRanges = apiSession.sliceRanges( extractionId );
        if ( !apiRanges ) {
            LOG_WARNING << "populateEventsFromExtraction: sliceRanges failed: "
                        << apiRanges.error().message;
            return result;
        }
        for ( const auto& apiRange : *apiRanges ) {
            const auto category = QString::fromStdString( apiRange.category );
            if ( !categorySelected( category ) ) {
                continue;
            }

            SliceLineRange range;
            range.ruleName = apiRange.name;
            range.category = apiRange.category;
            range.traceId = apiRange.traceId;
            range.beginLine = apiRange.beginLine;
            range.endLine = apiRange.endLine;

            const auto beginIt = eventsByLine.find( apiRange.beginLine );
            const auto endIt = eventsByLine.find( apiRange.endLine );
            const auto* beginEvent = beginIt != eventsByLine.end() ? beginIt->second : nullptr;
            const auto* endEvent = endIt != eventsByLine.end() ? endIt->second : nullptr;

            if ( beginEvent ) {
                range.description = beginEvent->description;
                range.pid = beginEvent->pid;
                range.tid = beginEvent->tid;
            }

            if ( apiRange.beginLine == apiRange.endLine && beginEvent
                 && beginEvent->eventType == klogg::api::EventType::Slice
                 && beginEvent->sliceType == klogg::api::SliceType::None ) {
                // Complete slice: the single event carries the duration
                range.startTimestamp = beginEvent->startTimestampNs;
                range.endTimestamp = beginEvent->timestampNs;
                range.duration = beginEvent->durationNs;
            }
            else if ( endEvent && endEvent->sliceType == klogg::api::SliceType::End ) {
                range.startTimestamp = beginEvent ? beginEvent->timestampNs : 0;
                range.endTimestamp = endEvent->timestampNs;
                range.duration = qMax<qint64>( 0, range.endTimestamp - range.startTimestamp );
            }
            else {
                // Begin without a matching end extends to the end of the
                // scanned range
                range.startTimestamp = beginEvent ? beginEvent->timestampNs : 0;
                range.openEnded = true;
            }
            result.sliceRanges.append( range );
        }

        result.ok = true;
        return result;
    } ) );
}

void CrawlerWidget::clearSliceRangeIndex()
{
    LOG_INFO << "clearSliceRangeIndex: previous sliceRanges=" << sliceRanges_.size()
             << " starts=" << sliceStartsByLine_.size() << " ends=" << sliceEndsByLine_.size();
    sliceRanges_.clear();
    sliceStartsByLine_.clear();
    sliceEndsByLine_.clear();
    activeSliceIndicesCache_.clear();
    activeSliceCacheValid_ = false;
}

QVector<SliceLineRange> CrawlerWidget::slicesAtSourceLine( LineNumber line ) const
{
    QVector<SliceLineRange> result;
    if ( sliceRanges_.isEmpty() ) {
        LOG_DEBUG << "slicesAtSourceLine: no slice ranges for line=" << line.get();
        return result;
    }

    auto rebuildCacheToLine = [ this ]( LineNumber targetLine ) {
        activeSliceIndicesCache_.clear();

        for ( int i = 0; i < sliceRanges_.size(); ++i ) {
            const auto& range = sliceRanges_[ i ];
            if ( range.beginLine <= targetLine && targetLine <= range.endLine ) {
                activeSliceIndicesCache_.append( i );
            }
        }

        activeSliceCacheLine_ = targetLine;
        activeSliceCacheValid_ = true;
    };

    if ( !activeSliceCacheValid_ ) {
        rebuildCacheToLine( line );
    }
    else if ( line != activeSliceCacheLine_ ) {
        const auto diff = static_cast<qint64>( line.get() )
                          - static_cast<qint64>( activeSliceCacheLine_.get() );
        if ( std::llabs( diff ) > 64 ) {
            rebuildCacheToLine( line );
        }
        else {
            qint64 current = static_cast<qint64>( activeSliceCacheLine_.get() );
            const qint64 target = static_cast<qint64>( line.get() );

            while ( current < target ) {
                ++current;
                const auto ended = sliceEndsByLine_.value( static_cast<qulonglong>( current - 1 ) );
                for ( int endedIndex : ended ) {
                    activeSliceIndicesCache_.removeAll( endedIndex );
                }
                const auto started = sliceStartsByLine_.value( static_cast<qulonglong>( current ) );
                for ( int startedIndex : started ) {
                    activeSliceIndicesCache_.append( startedIndex );
                }
            }

            while ( current > target ) {
                const auto started = sliceStartsByLine_.value( static_cast<qulonglong>( current ) );
                for ( int startedIndex : started ) {
                    activeSliceIndicesCache_.removeAll( startedIndex );
                }
                const auto ended = sliceEndsByLine_.value( static_cast<qulonglong>( current - 1 ) );
                for ( int endedIndex : ended ) {
                    activeSliceIndicesCache_.append( endedIndex );
                }
                --current;
            }

            activeSliceCacheLine_ = line;
        }
    }

    QVector<int> matchedIndices = activeSliceIndicesCache_;

    std::sort( matchedIndices.begin(), matchedIndices.end(), [ this ]( int lhs, int rhs ) {
        const auto& a = sliceRanges_[ lhs ];
        const auto& b = sliceRanges_[ rhs ];
        if ( a.beginLine != b.beginLine ) {
            return a.beginLine < b.beginLine;
        }
        if ( a.endLine != b.endLine ) {
            return a.endLine > b.endLine;
        }
        return a.ruleName < b.ruleName;
    } );

    result.reserve( matchedIndices.size() );
    for ( int index : matchedIndices ) {
        result.append( sliceRanges_[ index ] );
    }

    LOG_DEBUG << "slicesAtSourceLine: line=" << line.get() << " matched=" << result.size()
              << " cacheValid=" << activeSliceCacheValid_
              << " cacheLine=" << activeSliceCacheLine_.get();
    return result;
}

void CrawlerWidget::emitSelectedSourceLineChanged( LineNumber line )
{
    if ( selectedSourceLine_.has_value() && *selectedSourceLine_ == line ) {
        LOG_DEBUG << "emitSelectedSourceLineChanged skipped: same line=" << line.get();
        return;
    }

    selectedSourceLine_ = line;
    LOG_DEBUG << "emitSelectedSourceLineChanged: line=" << line.get();
    Q_EMIT selectedSourceLineChanged( line );
}

int CrawlerWidget::rightSideReservedWidth() const
{
    int reservedWidth = 0;
    int scrollBarWidth = 0;
    int overviewWidth = 0;
    bool overviewVisible = false;
    int mainViewWidth = 0;

    if ( logMainView_ != nullptr ) {
        mainViewWidth = logMainView_->width();
        reservedWidth = qMax( reservedWidth, logMainView_->contentsMargins().right() );

        if ( auto* scrollBar = logMainView_->verticalScrollBar(); scrollBar != nullptr ) {
            scrollBarWidth = scrollBar->isVisible() ? scrollBar->width() : 0;
            reservedWidth += scrollBarWidth;
        }
    }

    if ( overviewWidget_ != nullptr && overviewWidget_->isVisible() ) {
        overviewVisible = true;
        overviewWidth = overviewWidget_->width();
        reservedWidth += overviewWidth;
    }

    LOG_DEBUG << "rightSideReservedWidth: reservedWidth=" << reservedWidth
              << " mainViewWidth=" << mainViewWidth << " scrollBarWidth=" << scrollBarWidth
              << " overviewVisible=" << overviewVisible << " overviewWidth=" << overviewWidth
              << " mainViewMargins="
              << ( logMainView_ != nullptr ? logMainView_->contentsMargins().left() : 0 ) << ","
              << ( logMainView_ != nullptr ? logMainView_->contentsMargins().top() : 0 ) << ","
              << ( logMainView_ != nullptr ? logMainView_->contentsMargins().right() : 0 ) << ","
              << ( logMainView_ != nullptr ? logMainView_->contentsMargins().bottom() : 0 );

    return reservedWidth;
}

QRect CrawlerWidget::sliceOverlayAnchorRect() const
{
    if ( logMainView_ == nullptr ) {
        LOG_WARNING << "sliceOverlayAnchorRect: missing logMainView";
        return {};
    }

    const QWidget* anchorWidget = logMainView_->viewport();
    if ( anchorWidget == nullptr ) {
        anchorWidget = logMainView_;
    }

    const QRect anchorWidgetRect( QPoint( 0, 0 ), anchorWidget->size() );
    const QPoint topLeftInCrawler = anchorWidget->mapTo( this, anchorWidgetRect.topLeft() );
    const QRect anchorRect( topLeftInCrawler, anchorWidgetRect.size() );

    LOG_DEBUG << "sliceOverlayAnchorRect: anchorWidget=" << anchorWidget
              << " rect=" << anchorRect.x() << "," << anchorRect.y() << "," << anchorRect.width()
              << "," << anchorRect.height() << " mainViewGeometry=" << logMainView_->geometry().x()
              << "," << logMainView_->geometry().y() << "," << logMainView_->geometry().width()
              << "," << logMainView_->geometry().height()
              << " viewportSize=" << anchorWidget->width() << "x" << anchorWidget->height();

    return anchorRect;
}

void CrawlerWidget::handleMainViewSelectedLine( LineNumber line, LinesCount, LineColumn,
                                                LineLength )
{
    LOG_DEBUG << "handleMainViewSelectedLine: line=" << line.get();
    emitSelectedSourceLineChanged( line );
}

void CrawlerWidget::handleFilteredViewSelectedLine( LineNumber line, LinesCount, LineColumn,
                                                    LineLength )
{
    const auto sourceLine = sourceLineForFilteredIndex( line );
    LOG_DEBUG << "handleFilteredViewSelectedLine: filteredLine=" << line.get()
              << " sourceLine=" << sourceLine.get();
    emitSelectedSourceLineChanged( sourceLine );
}

void CrawlerWidget::jumpToMatchingLine( LineNumber filteredLineNb, LinesCount nLines,
                                        LineColumn startCol, LineLength nSymbols )
{
    const auto mainViewLine = sourceLineForFilteredIndex( filteredLineNb );
    logMainView_->selectPortionAndDisplayLine( mainViewLine, nLines, startCol,
                                               nSymbols ); // FIXME: should be done with a signal.
}

void CrawlerWidget::updateLineNumberHandler( LineNumber line, LinesCount nLines,
                                             LineColumn startCol, LineLength nSymbols )
{
    currentLineNumber_ = line;
    Q_EMIT newSelection( line, nLines, startCol, nSymbols );
}

void CrawlerWidget::markLinesFromMain( const klogg::vector<LineNumber>& lines )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::markLinesFromMain" );
    auto& apiSession = klogg::ui::apiSession();

    // Session-level marks are the source of truth (shared per file)
    const auto marksResult = apiSession.listMarks( fileId_ );
    if ( !marksResult ) {
        LOG_ERROR << "listMarks failed: " << marksResult.error().message;
        return;
    }
    const std::set<LineNumber> markedLines( marksResult->begin(), marksResult->end() );

    std::vector<LineNumber> toMark;
    std::vector<LineNumber> toUnmark;
    for ( const auto& line : lines ) {
        if ( line >= cachedTotalLines_ ) {
            continue;
        }

        if ( markedLines.count( line ) ) {
            toUnmark.push_back( line );
        }
        else {
            toMark.push_back( line );
        }
    }

    // Mark the unmarked lines; only when every line was already marked,
    // unmark them all
    if ( !toMark.empty() ) {
        apiSession.addMarks( fileId_, toMark );
    }
    else {
        apiSession.removeMarks( fileId_, toUnmark );
    }

    mirrorMarksToCurrentData();

    // Recompute the content of both window.
    filteredView_->updateData();
    logMainView_->updateData();

    // Update the match overview
    overview_.updateData( cachedTotalLines_ );

    // Also update the top window for the coloured bullets.
    update();
}

void CrawlerWidget::markLinesFromFiltered( const klogg::vector<LineNumber>& lines )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::markLinesFromFiltered" );
    klogg::vector<LineNumber> linesInMain( lines.size() );
    std::transform( lines.cbegin(), lines.cend(), linesInMain.begin(),
                    [ this ]( const auto& filteredLine ) {
                        if ( filteredLine < cachedTotalLines_ ) {
                            return sourceLineForFilteredIndex( filteredLine );
                        }
                        else {
                            return maxValue<LineNumber>();
                        }
                    } );

    markLinesFromMain( linesInMain );
}

void CrawlerWidget::applyConfiguration()
{
    const auto& config = Configuration::get();
    QFont font = config.mainFont();

    LOG_DEBUG << "CrawlerWidget::applyConfiguration";

    registerShortcuts();

    // Whatever font we use, we should NOT use kerning
    font.setKerning( false );
    font.setFixedPitch( true );

    // Necessary on systems doing subpixel positionning (e.g. Ubuntu 12.04)
    if ( config.forceFontAntialiasing() ) {
        font.setStyleStrategy( QFont::PreferAntialias );
    }

    font.setBold( config.useBoldFont() );

    logMainView_->setLineNumbersVisible( config.mainLineNumbersVisible() );

    const auto isFollowModeAllowed = config.anyFileWatchEnabled();
    logMainView_->allowFollowMode( isFollowModeAllowed );
    overview_.setVisible( config.isOverviewVisible() );
    logMainView_->refreshOverview();
    logMainView_->updateFont( font );

    for ( auto i = 0; i < tabbedFilteredView_->count(); ++i ) {
        auto fv = qobject_cast<FilteredView*>( tabbedFilteredView_->widget( i ) );
        fv->setLineNumbersVisible( config.filteredLineNumbersVisible() );
        fv->allowFollowMode( isFollowModeAllowed );
        fv->updateFont( font );
    }

    // Update the SearchLine (history)
    updateSearchCombo();

    const auto& fileWatchConfig = Configuration::get();
    FileWatcher::getFileWatcher().setConfiguration(
        { fileWatchConfig.nativeFileWatchEnabled(), fileWatchConfig.pollingEnabled(),
          fileWatchConfig.pollIntervalMs() } );
    // Apply engine behavior parameters for subsequent opens/searches.
    klogg::ui::apiSession().updateOptions( fileWatchConfig.engineOptions() );

    if ( isFollowEnabled() ) {
        changeDataStatus( DataStatus::OLD_DATA );
    }

    reloadPredefinedFilters();
}

void CrawlerWidget::enteringQuickFind()
{
    LOG_DEBUG << "CrawlerWidget::enteringQuickFind";

    // Remember who had the focus (only if it is one of our views)
    QWidget* focus_widget = QApplication::focusWidget();

    if ( ( focus_widget == logMainView_ ) || ( focus_widget == filteredView_ ) )
        qfSavedFocus_ = focus_widget;
    else
        qfSavedFocus_ = nullptr;
}

void CrawlerWidget::exitingQuickFind()
{
    // Restore the focus once the QFBar has been hidden
    if ( qfSavedFocus_ )
        qfSavedFocus_->setFocus();
}

void CrawlerWidget::loadingFinishedHandler( LoadingStatus status )
{
    LOG_INFO << "file loading finished, status " << static_cast<int>( status );

    // The engine refreshed the fileInfo on completion; pick up the new total
    // before the overview and the search limits below read the cache
    refreshTotalLines();

    // We need to refresh the main window because the view lines on the
    // overview have probably changed.
    overview_.updateData( cachedTotalLines_ );

    // FIXME, handle topLine
    logMainView_->updateData();

    // Shall we Forbid starting a search when loading in progress?
    // searchButton_->setEnabled( false );

    // searchButton_->setEnabled( true );

    // See if we need to auto-refresh the search
    if ( searchState_.isAutorefreshAllowed() ) {
        searchEndLine_ = LineNumber( cachedTotalLines_.get() );
        // Full re-search with a fresh SearchId on file change (B3 decision):
        // the incremental updateSearch path is retired with API searches
        replaceCurrentSearch( getCurrentSearchText() );
    }

    // Set the encoding for the views
    updateEncoding();

    clearSearchLimits();

    // Also change the data available icon
    if ( firstLoadDone_ ) {
        // File reload invalidates event lines (the engine clears the
        // projections when the extraction source goes away)
        invalidateEventExtraction();
        clearSliceRangeIndex();
        changeDataStatus( DataStatus::NEW_DATA );
    }
    else {
        firstLoadDone_ = true;
        if ( !savedMarkedLines_.empty() ) {
            std::vector<LineNumber> marks( savedMarkedLines_.begin(), savedMarkedLines_.end() );
            klogg::ui::apiSession().addMarks( fileId_, marks );
            mirrorMarksToCurrentData();
        }
        logMainView_->setFocus();
    }

    loadingInProgress_ = false;
    Q_EMIT loadingFinished( status );
}

void CrawlerWidget::fileChangedHandler( MonitoredFileStatus status )
{
    // Truncation/reload changes the total; the engine may still be reindexing,
    // in which case the next loadingFinished refreshes the cache again
    refreshTotalLines();

    // Handle the case where the file has been truncated
    if ( status == MonitoredFileStatus::Truncated ) {
        // Clear all marks (TODO offer the option to keep them)
        const auto marks = klogg::ui::apiSession().listMarks( fileId_ );
        if ( marks && !marks->empty() ) {
            klogg::ui::apiSession().removeMarks( fileId_, *marks );
        }
        mirrorMarksToCurrentData();
        if ( !searchInfoLine_->text().isEmpty() ) {
            // Invalidate the search
            auto& viewData = currentViewData();
            cancelViewSearch( viewData );
            switchToEmptyData();
            searchState_.truncateFile();
            printSearchInfoMessage();
            nbMatches_ = 0_lcount;
        }
    }
}

// Returns a pointer to the window in which the search should be done
AbstractLogView* CrawlerWidget::activeView() const
{
    QWidget* activeView;

    // Search in the window that has focus, or the window where 'Find' was
    // called from, or the main window.
    if ( filteredView_->hasFocus() || logMainView_->hasFocus() )
        activeView = QApplication::focusWidget();
    else
        activeView = qfSavedFocus_;

    if ( activeView ) {
        auto* view = qobject_cast<AbstractLogView*>( activeView );
        return view;
    }
    else {
        LOG_WARNING << "No active view, defaulting to logMainView";
        return logMainView_;
    }
}

void CrawlerWidget::searchForward()
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::searchForward" );
    LOG_DEBUG << "CrawlerWidget::searchForward";

    activeView()->searchForward();
}

void CrawlerWidget::searchBackward()
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::searchBackward" );
    LOG_DEBUG << "CrawlerWidget::searchBackward";

    activeView()->searchBackward();
}

LinesCount CrawlerWidget::currentMatchCount() const
{
    if ( !currentSearchId_ ) {
        return 0_lcount;
    }
    const auto progress = klogg::ui::apiSession().searchProgress( *currentSearchId_ );
    if ( !progress ) {
        LOG_WARNING << "CrawlerWidget: searchProgress failed: " << progress.error().message;
        return 0_lcount;
    }
    return progress->total_matches;
}

void CrawlerWidget::refreshTotalLines()
{
    const auto info = klogg::ui::apiSession().fileInfo( fileId_ );
    if ( info ) {
        cachedTotalLines_ = info->total_lines;
        return;
    }
    LOG_WARNING << "CrawlerWidget: fileInfo failed, keeping the cached line count: "
                << info.error().message;
}

LineNumber CrawlerWidget::filteredIndexFloor( LineNumber sourceLine ) const
{
    if ( currentSearchId_ ) {
        const auto index
            = klogg::ui::apiSession().filteredLineIndexAtOrBefore( *currentSearchId_, sourceLine );
        if ( index ) {
            // The native mapping clamps "before every member" to 0
            return LineNumber( static_cast<LineNumber::UnderlyingType>( index->value_or( 0 ) ) );
        }
        LOG_WARNING << "CrawlerWidget: filteredLineIndexAtOrBefore failed, "
                       "falling back to the local projection floor: "
                    << index.error().message;
    }
    // Search-less: local floor over the sorted empty projection, clamped to 0
    const auto it = std::upper_bound( emptyProjectionLines_.cbegin(), emptyProjectionLines_.cend(),
                                      sourceLine );
    if ( it == emptyProjectionLines_.cbegin() ) {
        return 0_lnum;
    }
    return LineNumber( static_cast<LineNumber::UnderlyingType>(
        std::distance( emptyProjectionLines_.cbegin(), it - 1 ) ) );
}

LineNumber CrawlerWidget::sourceLineForFilteredIndex( LineNumber index ) const
{
    if ( currentSearchId_ ) {
        const auto lines = klogg::ui::apiSession().getFilteredLines( *currentSearchId_, index.get(),
                                                                     1, klogg::api::AnsiMode::Raw );
        if ( lines ) {
            return lines->empty() ? maxValue<LineNumber>() : lines->front().num;
        }
        LOG_WARNING << "CrawlerWidget: getFilteredLines failed, "
                       "falling back to the local projection: "
                    << lines.error().message;
    }
    if ( index.get() < emptyProjectionLines_.size() ) {
        return emptyProjectionLines_[ index.get() ];
    }
    return maxValue<LineNumber>();
}

QString CrawlerWidget::eventTooltipAtSourceLine( LineNumber sourceLine ) const
{
    // The map keys exactly the event lines of the populated extraction
    return eventTooltipsByLine_.value( sourceLine );
}

QString CrawlerWidget::eventTooltipAtFilteredLine( LineNumber viewLine ) const
{
    if ( currentSearchId_ ) {
        // The filtered view's line is a projection index: map it to the
        // source line the tooltip map is keyed by
        const auto lines = klogg::ui::apiSession().getFilteredLines(
            *currentSearchId_, viewLine.get(), 1, klogg::api::AnsiMode::Raw );
        if ( lines && !lines->empty() ) {
            return eventTooltipsByLine_.value( lines->front().num );
        }
        return {};
    }
    // The search-less projection indexes its sorted source lines
    if ( viewLine.get() < emptyProjectionLines_.size() ) {
        return eventTooltipsByLine_.value( emptyProjectionLines_[ viewLine.get() ] );
    }
    return {};
}

void CrawlerWidget::resetStateOnSearchPatternChanges()
{
    // We suspend auto-refresh

    searchState_.changeExpression();
    printSearchInfoMessage( currentMatchCount() );
}

void CrawlerWidget::searchRefreshChangedHandler( bool isRefreshing )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::searchRefreshChangedHandler" );
    searchState_.setAutorefresh( isRefreshing );
    printSearchInfoMessage( currentMatchCount() );
}

void CrawlerWidget::matchCaseChangedHandler( bool shouldMatchCase )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::matchCaseChangedHandler" );
    searchLineCompleter_->setCaseSensitivity( shouldMatchCase ? Qt::CaseSensitive
                                                              : Qt::CaseInsensitive );

    resetStateOnSearchPatternChanges();
}

void CrawlerWidget::booleanCombiningChangedHandler( bool )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::booleanCombiningChangedHandler" );
    if ( patternInputWidget_->isChipMode() ) {
        patternInputWidget_->setNotButtonVisible( booleanButton_->isChecked() );
    }
    patternInputWidget_->setBooleanMode( booleanButton_->isChecked() );
    // Manual toggle clears auto-enabled state
    booleanAutoEnabled_ = false;
    resetStateOnSearchPatternChanges();
}

void CrawlerWidget::useRegexpChangeHandler( bool )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::useRegexpChangeHandler" );
    patternInputWidget_->setRegexMode( useRegexpButton_->isChecked() );
    patternInputWidget_->setBooleanMode( booleanButton_->isChecked() );
    resetStateOnSearchPatternChanges();
}

void CrawlerWidget::searchTextChangeHandler( QString )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::searchTextChangeHandler" );
    if ( !patternInputWidget_->isChipMode() ) {
        const auto text = searchLineEdit_->lineEdit()->text();
        chipModeButton_->setEnabled( PatternInputWidget::canParseToChips( text ) );
    }
    resetStateOnSearchPatternChanges();
    updatePredefinedFiltersWidget();
}

void CrawlerWidget::changeFilteredViewVisibility( int index )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::changeFilteredViewVisibility" );
    QStandardItem* item = visibilityModel_->item( index );
    auto visibility = item->data().value<FilteredView::Visibility>();

    // Check if the new visibility includes Events
    bool includesEvents = visibility.testFlag( LogFilteredData::VisibilityFlags::Events );

    // Notify MainWindow about visibility change (for category button state)
    Q_EMIT visibilityChanged( includesEvents );

    // Set visibility and update view
    // Note: We don't clear data on visibility change - data persists like marks/matches
    filteredView_->setVisibility( visibility );
    activeSliceCacheValid_ = false;
    // The search-less projection's class mix follows the same combo
    rebuildEmptyProjection();

    if ( includesEvents ) {
        // Switching to a view that includes Events; the rendered set mirrors
        // the populated extraction, so its API-side count is the check
        bool hasExistingEvents = false;
        if ( populatedExtractionId_ ) {
            const auto extraction
                = klogg::ui::apiSession().extractionProgress( *populatedExtractionId_ );
            hasExistingEvents = extraction && extraction->eventsFound > 0;
        }
        if ( !hasExistingEvents ) {
            // No existing events - extract via the Core API
            performEventSearch();
        }
        // If we have existing events, just use them (no re-extraction needed)
    }
    // Visibility controls presentation only; it must not destroy data layers.

    LinesCount nbFilteredLines = 0_lcount;
    if ( currentSearchId_ ) {
        const auto count = klogg::ui::apiSession().getFilteredLineCount( *currentSearchId_ );
        if ( count ) {
            nbFilteredLines = *count;
        }
        else {
            LOG_WARNING << "CrawlerWidget: getFilteredLineCount failed: " << count.error().message;
        }
    }
    else {
        // Search-less state: the empty projection (marks union event lines)
        nbFilteredLines
            = LinesCount( static_cast<LinesCount::UnderlyingType>( emptyProjectionLines_.size() ) );
    }

    if ( nbFilteredLines > 0_lcount ) {
        filteredView_->selectAndDisplayLine( filteredIndexFloor( currentLineNumber_ ) );
    }
}

void CrawlerWidget::performEventSearch()
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::performEventSearch" );
    // FileId 0 means no file is attached to the session yet
    if ( fileId_ == 0 ) {
        return;
    }

    auto& apiSession = klogg::ui::apiSession();

    // A running extraction populates the view on completion; a completed one
    // is re-applied right away (e.g. the category filter changed)
    if ( eventExtractionId_ ) {
        const auto progress = apiSession.extractionProgress( *eventExtractionId_ );
        if ( progress && progress->progress < 1.0f ) {
            searchInfoLine_->setText( tr( "Searching for events..." ) );
            searchInfoLine_->show();
            return;
        }
        if ( progress && progress->progress >= 1.0f ) {
            populateEventsFromExtraction();
            return;
        }
        // Cancelled or unknown handle: fall through and start over
        eventExtractionId_.reset();
    }

    const auto configs = klogg::ui::listLogTypeConfigs();
    if ( !klogg::ui::hasAnyParsingRule( configs ) ) {
        searchInfoLine_->setText(
            tr( "No log parsing rules configured. Configure them in Tools > Log Parsing Rules." ) );
        searchInfoLine_->show();
        return;
    }
    if ( klogg::ui::allEventRules( configs ).isEmpty() ) {
        searchInfoLine_->setText( tr( "No event rules. Use 'Mark as Event' to create rules." ) );
        searchInfoLine_->show();
        return;
    }

    const auto fileInfo = apiSession.fileInfo( fileId_ );
    if ( !fileInfo ) {
        LOG_ERROR << "performEventSearch: fileInfo failed: " << fileInfo.error().message;
        return;
    }
    const auto matchedConfig = klogg::ui::matchLogTypeForFile( fileInfo->path );
    if ( !matchedConfig ) {
        searchInfoLine_->setText( tr( "No event rules. Use 'Mark as Event' to create rules." ) );
        searchInfoLine_->show();
        return;
    }
    const auto& config = *matchedConfig;

    // The API extracts with the whole matched config; the category selection
    // is applied when events are pulled into the view. The pre-check keeps
    // the legacy "nothing to do" messages.
    bool hasEnabledRules = false;
    for ( const auto& rule : klogg::ui::allEventRules( { config } ) ) {
        if ( !rule.enabled ) {
            continue;
        }
        if ( !currentEventCategories_.isEmpty()
             && !currentEventCategories_.contains( QString::fromStdString( rule.category ) ) ) {
            continue;
        }
        hasEnabledRules = true;
        break;
    }
    if ( !hasEnabledRules ) {
        searchInfoLine_->setText( tr( "No enabled event rules." ) );
        searchInfoLine_->show();
        return;
    }

    klogg::api::ExtractionParams params;
    params.logTypeConfigName = config.name;
    const auto extraction = apiSession.extractEvents( fileId_, params );
    if ( !extraction ) {
        LOG_ERROR << "performEventSearch: extractEvents failed: " << extraction.error().message;
        searchInfoLine_->setText( tr( "No enabled event rules." ) );
        searchInfoLine_->show();
        return;
    }

    eventExtractionId_ = *extraction;
    populatedExtractionId_.reset();
    ensureExtractionEventSubscription();
    registerEventRoute( *extraction );

    searchInfoLine_->setText( tr( "Searching for events..." ) );
    searchInfoLine_->show();

    // A tiny file may complete before the routed events arrive; deliver the
    // final state directly (same pattern as adoptSearchData)
    const auto progress = apiSession.extractionProgress( *extraction );
    if ( progress && progress->progress >= 1.0f ) {
        handleEventExtractionCompleted( *extraction, progress->eventsFound );
    }
}

void CrawlerWidget::updateEventView()
{
    // Event categories define the Events dataset regardless of current visibility.
    // Visibility only controls whether that dataset is currently projected.
    performEventSearch();
}

void CrawlerWidget::setCurrentEventCategories( const QStringList& categories )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::setCurrentEventCategories" );
    currentEventCategories_ = categories;
    activeSliceCacheValid_ = false;

    // The engine mirrors only the selected categories into the search
    // projections; the gutter event bit stays unfiltered
    if ( fileId_ != 0 ) {
        std::vector<std::string> apiCategories;
        apiCategories.reserve( static_cast<size_t>( categories.size() ) );
        for ( const auto& category : categories ) {
            apiCategories.push_back( category.toStdString() );
        }
        const auto applied
            = klogg::ui::apiSession().setEventCategoryFilter( fileId_, std::move( apiCategories ) );
        if ( !applied ) {
            LOG_WARNING << "setEventCategoryFilter failed: " << applied.error().message;
        }
    }
}

bool CrawlerWidget::isEventVisibility() const
{
    if ( !filteredView_ || !visibilityModel_ ) {
        return false;
    }

    int currentIndex = visibilityBox_->currentIndex();
    if ( currentIndex < 0 || currentIndex >= visibilityModel_->rowCount() ) {
        return false;
    }

    QStandardItem* item = visibilityModel_->item( currentIndex );
    auto visibility = item->data().value<FilteredView::Visibility>();
    return visibility.testFlag( LogFilteredData::VisibilityFlags::Events );
}

int CrawlerWidget::findVisibilityIndex( const QStandardItemModel* model,
                                        FilteredView::Visibility visibility )
{
    if ( model == nullptr ) {
        return -1;
    }

    for ( int i = 0; i < model->rowCount(); ++i ) {
        const auto* item = model->item( i );
        if ( item == nullptr ) {
            continue;
        }

        const auto rowVisibility = item->data().value<FilteredView::Visibility>();
        if ( rowVisibility == visibility ) {
            return i;
        }
    }

    return -1;
}

void CrawlerWidget::setSearchPatternFromPredefinedFilters(
    const klogg::vector<PredefinedFilter>& filters )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::setSearchPatternFromPredefinedFilters" );
    QString searchPattern;
    QVector<Chip> chipList;
    chipList.reserve( klogg::isize( filters ) );

    for ( const auto& filter : filters ) {
        const auto pattern = QString::fromStdString( filter.pattern );
        const auto escapedPattern = escapeSearchPattern( pattern, filter.useRegex );
        combinePatterns( searchPattern, escapedPattern );
        const auto chipPatternText
            = useRegexpButton_->isChecked() && !filter.useRegex ? QRegularExpression::escape( pattern )
                                                                : pattern;
        chipList.append( Chip{ ChipType::Or, { chipPatternText } } );
    }

    if ( patternInputWidget_->isChipMode() ) {
        searchLineEdit_->setEditText( searchPattern );
        patternInputWidget_->setChips( chipList );
        updatePredefinedFiltersWidget();
        focusSearchEdit();

        if ( Configuration::get().autoRunSearchOnPatternChange() ) {
            dispatchToMainThread( [ this ] { startNewSearch(); } );
        }

        return;
    }

    setSearchPattern( searchPattern );
}

QString CrawlerWidget::escapeSearchPattern( const QString& pattern, bool isRegex ) const
{
    auto escapedPattern = ( !isRegex && useRegexpButton_->isChecked() )
                              ? QRegularExpression::escape( pattern )
                              : pattern;

    if ( booleanButton_->isChecked() ) {
        escapedPattern.replace( '"', "\"" ).prepend( '"' ).append( '"' );
    }

    return escapedPattern;
}

QString& CrawlerWidget::combinePatterns( QString& currentPattern, const QString& newPattern ) const
{
    if ( !currentPattern.isEmpty() ) {
        currentPattern.append( QStringLiteral( " or " ) );
    }

    currentPattern.append( newPattern );

    return currentPattern;
}

void CrawlerWidget::addToSearch( const QString& searchString )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::addToSearch" );
    if ( patternInputWidget_->isChipMode() ) {
        Chip newChip{ ChipType::Or, { searchString } };
        auto chips = patternInputWidget_->chips();
        chips.append( newChip );
        patternInputWidget_->setChips( chips );
        if ( Configuration::get().autoRunSearchOnPatternChange() ) {
            dispatchToMainThread( [ this ] { startNewSearch(); } );
        }
        return;
    }

    const auto newPattern = escapeSearchPattern( searchString );
    QString currentPattern = searchLineEdit_->currentText();
    setSearchPattern( combinePatterns( currentPattern, newPattern ) );
}

void CrawlerWidget::excludeFromSearch( const QString& searchString )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::excludeFromSearch" );
    if ( patternInputWidget_->isChipMode() ) {
        // Chip mode path: add NOT chip directly (raw text, quoting handled by combinePatterns)
        auto chips = patternInputWidget_->chips();
        chips.append( Chip{ ChipType::Not, { searchString } } );

        // Auto-enable boolean mode
        if ( !booleanButton_->isChecked() ) {
            booleanButton_->setChecked( true );
            booleanAutoEnabled_ = true;
        }

        patternInputWidget_->setChips( chips );
        updatePredefinedFiltersWidget();
        focusSearchEdit();

        if ( Configuration::get().autoRunSearchOnPatternChange() ) {
            dispatchToMainThread( [ this ] { startNewSearch(); } );
        }
        return;
    }

    // Text mode path: construct boolean expression
    QString currentPattern = searchLineEdit_->currentText();

    const auto wasInBooleanCombinationMode = booleanButton_->isChecked();
    if ( !wasInBooleanCombinationMode ) {
        currentPattern.replace( '"', "\"" ).prepend( '"' ).append( '"' );
    }

    booleanButton_->setChecked( true );

    const auto newPattern = escapeSearchPattern( searchString );

    if ( !currentPattern.isEmpty() ) {
        currentPattern.append( " and " );
    }

    currentPattern.append( "not(" ).append( newPattern ).append( ')' );
    setSearchPattern( currentPattern );
}

void CrawlerWidget::replaceSearch( const QString& searchString )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::replaceSearch" );
    setSearchPattern( escapeSearchPattern( searchString ) );
}

void CrawlerWidget::setSearchPattern( const QString& searchPattern )
{
    searchLineEdit_->setEditText( searchPattern );
    patternInputWidget_->setText( searchPattern );
    updatePredefinedFiltersWidget();
    // Set the focus to the visible search input so that the user can press 'Return' immediately
    focusSearchEdit();

    if ( Configuration::get().autoRunSearchOnPatternChange() ) {
        dispatchToMainThread( [ this ] { startNewSearch(); } );
    }
}

QString CrawlerWidget::getCurrentSearchText() const
{
    if ( patternInputWidget_->isChipMode() ) {
        return patternInputWidget_->text();
    }
    return searchLineEdit_->lineEdit()->text();
}

void CrawlerWidget::setChipMode( bool enabled )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::setChipMode" );
    patternInputWidget_->setRegexMode( useRegexpButton_->isChecked() );
    patternInputWidget_->setBooleanMode( booleanButton_->isChecked() );

    if ( enabled ) {
        // Determine text to parse
        QString textToParse;
        if ( !lastChipExpression_.isEmpty() ) {
            textToParse = std::exchange( lastChipExpression_, {} );
        }
        else {
            textToParse = searchLineEdit_->lineEdit()->text();
        }

        if ( !textToParse.isEmpty() && !PatternInputWidget::canParseToChips( textToParse ) ) {
            chipModeButton_->blockSignals( true );
            chipModeButton_->setChecked( false );
            chipModeButton_->blockSignals( false );
            chipModeButton_->setEnabled( false );
            return;
        }

        // Switch widget to chip mode UI, then populate chips from text
        patternInputWidget_->setChipMode( true );
        if ( !textToParse.isEmpty() ) {
            patternInputWidget_->setText( textToParse );
        }
    }
    else {
        // Save expression before turning off
        lastChipExpression_ = patternInputWidget_->text();
        patternInputWidget_->setChipMode( false );
    }

    // Sync button state
    chipModeButton_->blockSignals( true );
    chipModeButton_->setChecked( enabled );
    chipModeButton_->blockSignals( false );

    if ( enabled ) {
        patternInputWidget_->setNotButtonVisible( booleanButton_->isChecked() );
        patternInputWidget_->setSearchCompleter( searchLineCompleter_ );
        if ( !chipHistoryConnection_ ) {
            chipHistoryConnection_ = connect(
                searchLineCompleter_, QOverload<const QString&>::of( &QCompleter::activated ), this,
                [ this ]( const QString& text ) {
                    QTimer::singleShot( 0, this, [ this, text ]() {
                        patternInputWidget_->setText( text );
                        if ( Configuration::get().autoRunSearchOnPatternChange() ) {
                            dispatchToMainThread( [ this ] { startNewSearch(); } );
                        }
                    } );
                } );
        }
        searchLineEdit_->hide();
        patternInputWidget_->show();
        setFocusProxy( patternInputWidget_ );
    }
    else {
        chipModeButton_->setEnabled( true );
        disconnect( chipHistoryConnection_ );
        searchLineEdit_->setEditText( lastChipExpression_ );
        searchLineEdit_->setCompleter( searchLineCompleter_ );
        patternInputWidget_->hide();
        searchLineEdit_->show();
        setFocusProxy( searchLineEdit_ );
    }
}

bool CrawlerWidget::isChipMode() const
{
    return patternInputWidget_->isChipMode();
}

void CrawlerWidget::mouseHoveredOverMatch( LineNumber line )
{
    const auto line_in_mainview = sourceLineForFilteredIndex( line );

    overviewWidget_->highlightLine( line_in_mainview );
}

void CrawlerWidget::activityDetected()
{
    changeDataStatus( DataStatus::OLD_DATA );
}

void CrawlerWidget::setSearchLimits( LineNumber startLine, LineNumber endLine )
{
    searchStartLine_ = startLine;
    searchEndLine_ = endLine;

    logMainView_->setSearchLimits( startLine, endLine );
    filteredView_->setSearchLimits( startLine, endLine );
}

void CrawlerWidget::clearSearchLimits()
{
    setSearchLimits( 0_lnum, LineNumber( cachedTotalLines_.get() ) );
}

//
// Private functions
//

// Build the widget and connect all the signals, this must be done once
// the data are attached.
void CrawlerWidget::setup()
{
    LOG_INFO << "Setup crawler widget";
    setOrientation( Qt::Vertical );

    // The views
    auto bottomWindow = new QWidget;
    bottomWindow->setContentsMargins( 2, 0, 2, 0 );

    overviewWidget_ = new OverviewWidget();
    logMainView_ = new LogMainView( quickFindPattern_.get(), &overview_, overviewWidget_, fileId_ );
    logMainView_->setContentsMargins( 2, 0, 2, 0 );

    filteredView_ = new FilteredView( quickFindPattern_.get(), fileId_ );
    filteredViewsData_[ filteredView_ ] = { std::nullopt };
    filteredView_->setContentsMargins( 2, 0, 2, 0 );

    overviewWidget_->setOverview( &overview_ );
    overviewWidget_->setParent( logMainView_ );

    // Event tooltips come from this widget's per-tab map (source-line keyed)
    logMainView_->setEventTooltipProvider(
        [ this ]( LineNumber line ) { return eventTooltipAtSourceLine( line ); } );

    // Connect the search to the top view
    logMainView_->useNewFiltering( std::nullopt );

    // Construct the visibility button
    using VisibilityFlags = LogFilteredData::VisibilityFlags;
    visibilityModel_ = new QStandardItemModel( this );

    QStandardItem* marksMatchesEventsItem = new QStandardItem( tr( "Marks+Matches+Events" ) );
    marksMatchesEventsItem->setData( QVariant::fromValue(
        VisibilityFlags::Marks | VisibilityFlags::Matches | VisibilityFlags::Events ) );
    visibilityModel_->appendRow( marksMatchesEventsItem );

    QStandardItem* marksAndMatchesItem = new QStandardItem( tr( "Marks+Matches" ) );
    marksAndMatchesItem->setData(
        QVariant::fromValue( VisibilityFlags::Marks | VisibilityFlags::Matches ) );
    visibilityModel_->appendRow( marksAndMatchesItem );

    QStandardItem* marksAndEventsItem = new QStandardItem( tr( "Marks+Events" ) );
    marksAndEventsItem->setData(
        QVariant::fromValue( VisibilityFlags::Marks | VisibilityFlags::Events ) );
    visibilityModel_->appendRow( marksAndEventsItem );

    QStandardItem* matchesAndEventsItem = new QStandardItem( tr( "Matches+Events" ) );
    matchesAndEventsItem->setData(
        QVariant::fromValue( VisibilityFlags::Matches | VisibilityFlags::Events ) );
    visibilityModel_->appendRow( matchesAndEventsItem );

    QStandardItem* marksItem = new QStandardItem( tr( "Marks" ) );
    marksItem->setData( QVariant::fromValue<FilteredView::Visibility>( VisibilityFlags::Marks ) );
    visibilityModel_->appendRow( marksItem );

    QStandardItem* matchesItem = new QStandardItem( tr( "Matches" ) );
    matchesItem->setData(
        QVariant::fromValue<FilteredView::Visibility>( VisibilityFlags::Matches ) );
    visibilityModel_->appendRow( matchesItem );

    QStandardItem* eventsItem = new QStandardItem( tr( "Events" ) );
    eventsItem->setData( QVariant::fromValue<FilteredView::Visibility>( VisibilityFlags::Events ) );
    visibilityModel_->appendRow( eventsItem );

    auto* visibilityView = new QListView( this );
    visibilityView->setMovement( QListView::Static );
    // visibilityView->setMinimumWidth( 170 ); // Only needed with custom style-sheet

    visibilityBox_ = new QComboBox();
    visibilityBox_->setModel( visibilityModel_ );
    visibilityBox_->setView( visibilityView );

    visibilityBox_->setCurrentIndex( 1 ); // Default to Marks+Matches
    visibilityBox_->setContentsMargins( 2, 2, 2, 2 );

    // TODO: Maybe there is some way to set the popup width to be
    // sized-to-content (as it is when the stylesheet is not overriden) in the
    // stylesheet as opposed to setting a hard min-width on the view above.
    /*visibilityBox_->setStyleSheet( " \
        QComboBox:on {\
            padding: 1px 2px 1px 6px;\
            width: 19px;\
        } \
        QComboBox:!on {\
            padding: 1px 2px 1px 7px;\
            width: 19px;\
            height: 16px;\
            border: 1px solid gray;\
        } \
        QComboBox::drop-down::down-arrow {\
            width: 0px;\
            border-width: 0px;\
        } \
" );*/

    // Construct the Search Info line
    searchInfoLine_ = new InfoLine();
    searchInfoLine_->setFrameStyle( QFrame::StyledPanel );
    searchInfoLine_->setFrameShadow( QFrame::Sunken );
    searchInfoLine_->setLineWidth( 1 );
    searchInfoLine_->setSizePolicy( QSizePolicy::Minimum, QSizePolicy::Minimum );
    auto searchInfoLineSizePolicy = searchInfoLine_->sizePolicy();
    searchInfoLineSizePolicy.setRetainSizeWhenHidden( false );
    searchInfoLine_->setSizePolicy( searchInfoLineSizePolicy );
    searchInfoLineDefaultPalette_ = this->palette();
    searchInfoLine_->setContentsMargins( 2, 2, 2, 2 );

    matchCaseButton_ = new QToolButton();
    matchCaseButton_->setToolTip( tr( "Match case" ) );
    matchCaseButton_->setCheckable( true );
    matchCaseButton_->setFocusPolicy( Qt::NoFocus );
    matchCaseButton_->setContentsMargins( 2, 2, 2, 2 );

    useRegexpButton_ = new QToolButton();
    useRegexpButton_->setToolTip( tr( "Use regex" ) );
    useRegexpButton_->setCheckable( true );
    useRegexpButton_->setFocusPolicy( Qt::NoFocus );
    useRegexpButton_->setContentsMargins( 2, 2, 2, 2 );

    inverseButton_ = new QToolButton();
    inverseButton_->setToolTip( tr( "Inverse match" ) );
    inverseButton_->setCheckable( true );
    inverseButton_->setFocusPolicy( Qt::NoFocus );
    inverseButton_->setContentsMargins( 2, 2, 2, 2 );

    booleanButton_ = new QToolButton();
    booleanButton_->setToolTip( tr( "Enable regular expression logical combining" ) );
    booleanButton_->setCheckable( true );
    booleanButton_->setFocusPolicy( Qt::NoFocus );
    booleanButton_->setContentsMargins( 2, 2, 2, 2 );

    searchRefreshButton_ = new QToolButton();
    searchRefreshButton_->setToolTip( tr( "Auto-refresh" ) );
    searchRefreshButton_->setCheckable( true );
    searchRefreshButton_->setFocusPolicy( Qt::NoFocus );
    searchRefreshButton_->setContentsMargins( 2, 2, 2, 2 );

    chipModeButton_ = new QToolButton();
    chipModeButton_->setToolTip( tr( "Toggle chip input mode" ) );
    chipModeButton_->setText( QStringLiteral( "Chip" ) );
    chipModeButton_->setCheckable( true );
    chipModeButton_->setFocusPolicy( Qt::NoFocus );
    chipModeButton_->setContentsMargins( 2, 2, 2, 2 );

    // Construct the Search line
    searchLineCompleter_ = new QCompleter( searchHistoryItems(), this );
    searchLineEdit_ = new QComboBox;
    searchLineEdit_->setEditable( true );
    searchLineEdit_->setCompleter( searchLineCompleter_ );
    searchLineEdit_->addItems( searchHistoryItems() );
    searchLineEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );
    searchLineEdit_->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
    searchLineEdit_->lineEdit()->setMaxLength( std::numeric_limits<int>::max() / 1024 );
    searchLineEdit_->setContentsMargins( 2, 2, 2, 2 );

    patternInputWidget_ = new PatternInputWidget( this );
    patternInputWidget_->setPlaceholderText( tr( "Enter search pattern..." ) );
    patternInputWidget_->hide();

    QAction* clearSearchHistoryAction = new QAction( tr( "Clear search history" ), this );
    QAction* editSearchHistoryAction = new QAction( tr( "Edit search history" ), this );
    QAction* saveAsPredefinedFilterAction = new QAction( tr( "Save as Filter" ), this );

    clearSearchHistoryAction_ = clearSearchHistoryAction;
    editSearchHistoryAction_ = editSearchHistoryAction;
    saveAsPredefinedFilterAction_ = saveAsPredefinedFilterAction;

    searchLineContextMenu_ = searchLineEdit_->lineEdit()->createStandardContextMenu();
    searchLineContextMenu_->addSeparator();
    searchLineContextMenu_->addAction( saveAsPredefinedFilterAction );
    searchLineContextMenu_->addSeparator();
    searchLineContextMenu_->addAction( editSearchHistoryAction );
    searchLineContextMenu_->addAction( clearSearchHistoryAction );
    searchLineEdit_->setContextMenuPolicy( Qt::CustomContextMenu );

    setFocusProxy( searchLineEdit_ );

    clearButton_ = new QToolButton();
    clearButton_->setText( tr( "Clear search text" ) );
    clearButton_->setAutoRaise( true );
    clearButton_->setContentsMargins( 2, 2, 2, 2 );

    searchButton_ = new QToolButton();
    searchButton_->setText( tr( "Search" ) );
    searchButton_->setAutoRaise( true );
    searchButton_->setContentsMargins( 2, 2, 2, 2 );

    keepSearchResultsButton_ = new QToolButton();
    keepSearchResultsButton_->setText( tr( "Keep Results" ) );
    keepSearchResultsButton_->setToolTip(
        tr( "Keep these results and show subsequent results in a new window" ) );
    keepSearchResultsButton_->setCheckable( true );
    keepSearchResultsButton_->setContentsMargins( 2, 2, 2, 2 );

    stopButton_ = new QToolButton();
    stopButton_->setAutoRaise( true );
    stopButton_->setEnabled( false );
    stopButton_->setVisible( false );
    stopButton_->setContentsMargins( 2, 2, 2, 2 );

    predefinedFilters_ = new PredefinedFiltersComboBox( this );

    visibilityBox_->setSizeAdjustPolicy( QComboBox::AdjustToContents );
    predefinedFilters_->setSizeAdjustPolicy( QComboBox::AdjustToContents );

    auto* searchLineLayout = new QHBoxLayout;
    searchLineLayout->setContentsMargins( 2, 2, 2, 2 );

    searchLineLayout->addWidget( visibilityBox_ );
    searchLineLayout->addWidget( matchCaseButton_ );
    searchLineLayout->addWidget( useRegexpButton_ );
    searchLineLayout->addWidget( inverseButton_ );
    searchLineLayout->addWidget( booleanButton_ );
    searchLineLayout->addWidget( searchRefreshButton_ );
    searchLineLayout->addWidget( chipModeButton_ );
    searchLineLayout->addWidget( predefinedFilters_ );
    searchLineLayout->addWidget( searchLineEdit_ );
    searchLineLayout->addWidget( patternInputWidget_ );
    searchLineLayout->addWidget( clearButton_ );
    searchLineLayout->addWidget( searchButton_ );
    searchLineLayout->addWidget( keepSearchResultsButton_ );
    searchLineLayout->addWidget( stopButton_ );
    searchLineLayout->addWidget( searchInfoLine_ );

    // Construct the bottom window
    tabbedFilteredView_ = new QTabWidget;
    tabbedFilteredView_->setTabsClosable( true );
    tabbedFilteredView_->addTab( filteredView_, "" );
    tabbedFilteredView_->setDocumentMode( true );
    tabbedFilteredView_->setTabBarAutoHide( true );

    auto* bottomMainLayout = new QVBoxLayout;
    bottomMainLayout->addLayout( searchLineLayout );
    bottomMainLayout->addWidget( tabbedFilteredView_ );
    bottomMainLayout->setContentsMargins( 2, 2, 2, 2 );
    bottomWindow->setLayout( bottomMainLayout );

    addWidget( logMainView_ );
    addWidget( bottomWindow );

    // Default search checkboxes
    auto& config = Configuration::get();
    searchRefreshButton_->setChecked( config.isSearchAutoRefreshDefault() );
    matchCaseButton_->setChecked( !config.isSearchIgnoreCaseDefault() );
    useRegexpButton_->setChecked( config.mainRegexpType() == SearchRegexpType::ExtendedRegexp );
    booleanButton_->setChecked( config.isSearchLogicalCombiningDefault() );

    // Manually call the handler as it is not called when changing the state programmatically
    searchRefreshChangedHandler( searchRefreshButton_->isChecked() );
    useRegexpChangeHandler( useRegexpButton_->isChecked() );
    matchCaseChangedHandler( matchCaseButton_->isChecked() );
    booleanCombiningChangedHandler( booleanButton_->isChecked() );

    // Default splitter position (usually overridden by the config file)
    setSizes( config.splitterSizes() );

    applyToggleButtonPaletteStyle();

    registerShortcuts();
    loadIcons();

    // Connect the signals
    connect( searchLineEdit_->lineEdit(), &QLineEdit::returnPressed, searchButton_,
             &QToolButton::click );
    connect( searchLineEdit_->lineEdit(), &QLineEdit::textEdited, this,
             &CrawlerWidget::searchTextChangeHandler );

    connect( searchLineEdit_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             [ this ]( auto ) { updatePredefinedFiltersWidget(); } );

    connect( predefinedFilters_, &PredefinedFiltersComboBox::filterChanged, this,
             &CrawlerWidget::setSearchPatternFromPredefinedFilters );

    connect( searchLineEdit_, &QWidget::customContextMenuRequested, this,
             &CrawlerWidget::showSearchContextMenu );
    connect( saveAsPredefinedFilterAction, &QAction::triggered, this,
             &CrawlerWidget::saveAsPredefinedFilter );
    connect( clearSearchHistoryAction, &QAction::triggered, this,
             &CrawlerWidget::clearSearchHistory );
    connect( editSearchHistoryAction, &QAction::triggered, this,
             &CrawlerWidget::editSearchHistory );
    connect( searchButton_, &QToolButton::clicked, this, &CrawlerWidget::startNewSearch );
    connect( stopButton_, &QToolButton::clicked, this, &CrawlerWidget::stopSearch );
    connect( clearButton_, &QToolButton::clicked, searchLineEdit_, &QComboBox::clearEditText );
    connect( clearButton_, &QToolButton::clicked, patternInputWidget_, &PatternInputWidget::clear );
    connect( chipModeButton_, &QToolButton::toggled, this, &CrawlerWidget::setChipMode );

    connect( patternInputWidget_, &PatternInputWidget::textChanged, this,
             &CrawlerWidget::searchTextChangeHandler );
    connect( patternInputWidget_, &PatternInputWidget::returnPressed, searchButton_,
             &QToolButton::click );
    connect(
        patternInputWidget_, &PatternInputWidget::chipChanged, this, [ this ]( const QString& ) {
            if ( patternInputWidget_->isChipMode() ) {
                const auto chips = patternInputWidget_->chips();
                const bool hasExclude
                    = std::any_of( chips.cbegin(), chips.cend(), []( const Chip& c ) {
                          return c.type == ChipType::Not || c.type == ChipType::NotAndGroup;
                      } );
                const bool hasAndGroup
                    = std::any_of( chips.cbegin(), chips.cend(),
                                   []( const Chip& c ) { return c.type == ChipType::AndGroup; } );

                if ( hasExclude || hasAndGroup ) {
                    if ( !booleanButton_->isChecked() ) {
                        booleanButton_->setChecked( true );
                        booleanAutoEnabled_ = true;
                    }
                }
                else if ( booleanAutoEnabled_ && !hasExclude && !hasAndGroup ) {
                    booleanButton_->setChecked( false );
                    booleanAutoEnabled_ = false;
                }
            }

            resetStateOnSearchPatternChanges();
            updatePredefinedFiltersWidget();
            if ( Configuration::get().autoRunSearchOnPatternChange() ) {
                dispatchToMainThread( [ this ] { startNewSearch(); } );
            }
        } );

    connect( patternInputWidget_, &PatternInputWidget::notButtonToggled, this, [ this ]( bool ) {
        // NOT button pressed — auto-enable boolean if needed
        if ( patternInputWidget_->isChipMode() && !booleanButton_->isChecked() ) {
            booleanButton_->setChecked( true );
            booleanAutoEnabled_ = true;
        }
    } );

    connect( patternInputWidget_, &PatternInputWidget::contextMenuRequested, this,
             [ this ]( const QPoint& globalPos ) {
                 QMenu menu( this );
                 menu.addAction( saveAsPredefinedFilterAction_ );
                 menu.addSeparator();
                 menu.addAction( editSearchHistoryAction_ );
                 menu.addAction( clearSearchHistoryAction_ );
                 menu.exec( globalPos );
             } );

    connect( visibilityBox_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &CrawlerWidget::changeFilteredViewVisibility );

    connect( logMainView_, &LogMainView::newSelection,
             [ this ]( auto ) { logMainView_->update(); } );

    connect( logMainView_, &LogMainView::newSelection, this,
             &CrawlerWidget::updateLineNumberHandler );

    connect( logMainView_, &LogMainView::markLines, this, &CrawlerWidget::markLinesFromMain );

    connect( logMainView_, &LogMainView::highlightersChange, this,
             &CrawlerWidget::applyConfiguration );

    connect( logMainView_, QOverload<const QString&>::of( &LogMainView::addToSearch ), this,
             &CrawlerWidget::addToSearch );

    connect( logMainView_, QOverload<const QString&>::of( &LogMainView::excludeFromSearch ), this,
             &CrawlerWidget::excludeFromSearch );

    connect( logMainView_, QOverload<const QString&>::of( &LogMainView::replaceSearch ), this,
             &CrawlerWidget::replaceSearch );

    connect( logMainView_, &LogMainView::newSelection, this,
             &CrawlerWidget::handleMainViewSelectedLine );

    // Follow option (up and down)
    connect( this, &CrawlerWidget::followSet, logMainView_, &LogMainView::followSet );
    connect( logMainView_, &LogMainView::followModeChanged, this,
             &CrawlerWidget::followModeChanged );

    connect( this, &CrawlerWidget::textWrapSet, logMainView_, &LogMainView::textWrapSet );

    // Chip mode option
    connect( this, &CrawlerWidget::chipModeSet, this, &CrawlerWidget::setChipMode );

    // Detect activity in the views
    connect( logMainView_, &LogMainView::activity, this, &CrawlerWidget::activityDetected );

    connect( logMainView_, &LogMainView::changeSearchLimits, this,
             &CrawlerWidget::setSearchLimits );

    connect( logMainView_, &LogMainView::clearSearchLimits, this,
             &CrawlerWidget::clearSearchLimits );

    connect( tabbedFilteredView_, &QTabWidget::currentChanged, this,
             &CrawlerWidget::changeFilteredView );

    connect( tabbedFilteredView_, &QTabWidget::tabCloseRequested, this,
             &CrawlerWidget::closeFilteredView );

    connect( logMainView_, &LogMainView::saveDefaultSplitterSizes, this,
             &CrawlerWidget::saveSplitterSizes );

    connect( logMainView_, &LogMainView::changeFontSize, this, &CrawlerWidget::changeFontSize );

    // Matches progress comes through the API progress callback (registered
    // per search in adoptSearchData); extraction progress arrives through the
    // session event subscription (see ensureExtractionEventSubscription)

    // Load progress/completion and on-disk changes reach this widget through
    // the per-file session event route (see ensureFileEventSubscription and
    // doSetData); MainWindow gets them via the forwarded loading* signals

    // Search auto-refresh
    connect( searchRefreshButton_, &QPushButton::toggled, this,
             &CrawlerWidget::searchRefreshChangedHandler );

    connect( matchCaseButton_, &QPushButton::toggled, this,
             &CrawlerWidget::matchCaseChangedHandler );

    connect( useRegexpButton_, &QPushButton::toggled, this,
             &CrawlerWidget::useRegexpChangeHandler );

    connect( booleanButton_, &QPushButton::toggled, this,
             &CrawlerWidget::booleanCombiningChangedHandler );

    // Advise the parent the checkboxes have been changed
    // (for maintaining default config)
    connect( searchRefreshButton_, &QPushButton::toggled, this,
             &CrawlerWidget::searchRefreshChanged );
    connect( matchCaseButton_, &QPushButton::toggled, this, &CrawlerWidget::matchCaseChanged );

    // Switch between views
    connect( logMainView_, &AbstractLogView::clearColorLabels, this,
             &CrawlerWidget::clearColorLabels );

    connect( logMainView_, &AbstractLogView::addColorLabel, this,
             &CrawlerWidget::addColorLabelToSelection );

    connect( logMainView_, &AbstractLogView::sendSelectionToScratchpad, this,
             [ this ]() { Q_EMIT sendToScratchpad( logMainView_->getSelectedText() ); } );

    connect( logMainView_, &AbstractLogView::replaceScratchpadWithSelection, this,
             [ this ]() { Q_EMIT replaceDataInScratchpad( logMainView_->getSelectedText() ); } );

    connect( logMainView_, &AbstractLogView::eventRulesChanged, this,
             &CrawlerWidget::eventRulesChanged );

    connectAllFilteredViewSlots( filteredView_ );

    const auto defaultEncodingMib = config.defaultEncodingMib();
    if ( defaultEncodingMib >= 0 ) {
        encodingMib_ = defaultEncodingMib;
    }
    updatePredefinedFiltersWidget();

    // First push of the search-less projection to the initial tab
    rebuildEmptyProjection();
}

void CrawlerWidget::changeFilteredView( int tabIndex )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::changeFilteredView" );
    // Switching away stops the outgoing tab's ongoing search but keeps its
    // results for rendering; the outgoing tab may already have been removed
    // from the map when its widget was destroyed
    const auto outgoing = filteredViewsData_.find( filteredView_ );
    if ( outgoing != filteredViewsData_.end() ) {
        interruptViewSearch( outgoing->second );
    }

    if ( tabIndex >= 0 ) {
        auto* tabFilteredView
            = qobject_cast<FilteredView*>( tabbedFilteredView_->widget( tabIndex ) );

        filteredView_ = tabFilteredView;
        auto& viewData = filteredViewsData_.at( tabFilteredView );
        currentSearchId_ = viewData.searchId;

        Q_EMIT filteredViewChanged();

        logMainView_->useNewFiltering( viewData.searchId );
        // Session-level marks may have changed while the tab was inactive
        mirrorMarksToCurrentData();
        changeFilteredViewVisibility( visibilityBox_->currentIndex() );
    }
}

void CrawlerWidget::closeFilteredView( int tabIndex )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::closeFilteredView" );
    auto* tabFilteredView = tabbedFilteredView_->widget( tabIndex );
    connect( tabFilteredView, &QObject::destroyed, this, &CrawlerWidget::filteredViewDestroyed );
    tabFilteredView->deleteLater();
}

void CrawlerWidget::filteredViewDestroyed( QObject* view )
{
    // Tab closed: the creator cancels the search
    const auto it = filteredViewsData_.find( qobject_cast<FilteredView*>( view ) );
    if ( it != filteredViewsData_.end() ) {
        cancelViewSearch( it->second );
        filteredViewsData_.erase( it );
    }
}

void CrawlerWidget::saveSplitterSizes() const
{
    LOG_INFO << "Saving default splitter size";
    auto& splitterConfig = Configuration::get();
    splitterConfig.setSplitterSizes( sizes() );
    splitterConfig.save();
}

void CrawlerWidget::changeFontSize( bool increase )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::changeFontSize" );
    auto& fontConfig = Configuration::get();

    auto fontInfo = QFontInfo( fontConfig.mainFont() );
    const auto availableSizes = FontUtils::availableFontSizes( fontInfo.family() );

    auto currentSize
        = std::find( availableSizes.cbegin(), availableSizes.cend(), fontInfo.pointSize() );
    if ( increase && currentSize != std::prev( availableSizes.cend() ) ) {
        currentSize = std::next( currentSize );
    }
    else if ( !increase && currentSize != availableSizes.begin() ) {
        currentSize = std::prev( currentSize );
    }

    if ( currentSize != availableSizes.cend() ) {
        QFont newFont{ fontInfo.family(), *currentSize };

        fontConfig.setMainFont( newFont );
        logMainView_->updateFont( newFont );
        filteredView_->updateFont( newFont );
    }
}

void CrawlerWidget::connectAllFilteredViewSlots( FilteredView* view )
{
    connect( view, &FilteredView::newSelection, view, [ view ]( auto ) { view->update(); } );

    // Event tooltips come from this widget's per-tab map; the provider maps
    // the projection index to the source line it is keyed by
    view->setEventTooltipProvider(
        [ this ]( LineNumber line ) { return eventTooltipAtFilteredLine( line ); } );

    connect( view, &FilteredView::newSelection, this, &CrawlerWidget::jumpToMatchingLine );

    connect( view, &FilteredView::markLines, this, &CrawlerWidget::markLinesFromFiltered );

    connect( view, &FilteredView::highlightersChange, this, &CrawlerWidget::applyConfiguration );

    connect( view, QOverload<const QString&>::of( &FilteredView::addToSearch ), this,
             &CrawlerWidget::addToSearch );

    connect( view, QOverload<const QString&>::of( &FilteredView::excludeFromSearch ), this,
             &CrawlerWidget::excludeFromSearch );

    connect( view, QOverload<const QString&>::of( &FilteredView::replaceSearch ), this,
             &CrawlerWidget::replaceSearch );

    connect( view, &FilteredView::mouseHoveredOverLine, this,
             &CrawlerWidget::mouseHoveredOverMatch );

    connect( view, &FilteredView::newSelection, this,
             &CrawlerWidget::handleFilteredViewSelectedLine );

    connect( view, &FilteredView::mouseLeftHoveringZone, overviewWidget_,
             &OverviewWidget::removeHighlight );

    connect( this, &CrawlerWidget::followSet, view, &FilteredView::followSet );

    connect( view, &FilteredView::followModeChanged, this, &CrawlerWidget::followModeChanged );

    connect( this, &CrawlerWidget::textWrapSet, view, &FilteredView::textWrapSet );

    connect( view, &FilteredView::activity, this, &CrawlerWidget::activityDetected );

    connect( view, &FilteredView::changeSearchLimits, this, &CrawlerWidget::setSearchLimits );

    connect( view, &FilteredView::saveDefaultSplitterSizes, this,
             &CrawlerWidget::saveSplitterSizes );

    connect( view, &FilteredView::changeFontSize, this, &CrawlerWidget::changeFontSize );

    connect( view, &FilteredView::clearSearchLimits, this, &CrawlerWidget::clearSearchLimits );

    connect( view, &AbstractLogView::addColorLabel, this,
             &CrawlerWidget::addColorLabelToSelection );

    connect( view, &AbstractLogView::sendSelectionToScratchpad, this,
             [ view, this ]() { Q_EMIT sendToScratchpad( view->getSelectedText() ); } );

    connect( view, &AbstractLogView::replaceScratchpadWithSelection, this,
             [ view, this ]() { Q_EMIT replaceDataInScratchpad( view->getSelectedText() ); } );

    connect( view, &FilteredView::exitView, logMainView_,
             QOverload<>::of( &LogMainView::setFocus ) );

    connect( view, &AbstractLogView::clearColorLabels, this, &CrawlerWidget::clearColorLabels );

    connect( logMainView_, &LogMainView::exitView, view,
             QOverload<>::of( &FilteredView::setFocus ) );
}

void CrawlerWidget::registerShortcuts()
{
    LOG_INFO << "registering shortcuts for crawler widget";

    for ( auto& shortcut : shortcuts_ ) {
        shortcut.second->deleteLater();
    }

    shortcuts_.clear();

    const auto& config = Configuration::get();
    const auto& configuredShortcuts = config.shortcuts();

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerChangeVisibilityForward, [ this ]() {
            visibilityBox_->setCurrentIndex( ( visibilityBox_->currentIndex() + 1 )
                                             % visibilityBox_->count() );
        } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerEnableCaseMatching, [ this ]() { matchCaseButton_->toggle(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerEnableRegex, [ this ]() { useRegexpButton_->toggle(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerEnableInverseMatching, [ this ]() { inverseButton_->toggle(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerEnableRegexCombining, [ this ]() { booleanButton_->toggle(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerEnableAutoRefresh, [ this ]() { searchRefreshButton_->toggle(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerKeepResults, [ this ]() { keepSearchResultsButton_->toggle(); } );

    ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut,
                                      ShortcutAction::CrawlerChangeVisibilityBackward, [ this ]() {
                                          int nextIndex = visibilityBox_->currentIndex() - 1;
                                          if ( nextIndex < 0 ) {
                                              nextIndex = visibilityBox_->count() - 1;
                                          }
                                          visibilityBox_->setCurrentIndex( nextIndex );
                                      } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerChangeVisibilityToMarksAndMatches, [ this ]() {
            const int index = findVisibilityIndex(
                visibilityModel_,
                FilteredView::Visibility( LogFilteredData::VisibilityFlags::Marks
                                          | LogFilteredData::VisibilityFlags::Matches ) );
            if ( index >= 0 ) {
                visibilityBox_->setCurrentIndex( index );
            }
        } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerChangeVisibilityToMarks, [ this ]() {
            const int index = findVisibilityIndex(
                visibilityModel_,
                FilteredView::Visibility( LogFilteredData::VisibilityFlags::Marks ) );
            if ( index >= 0 ) {
                visibilityBox_->setCurrentIndex( index );
            }
        } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerChangeVisibilityToMatches, [ this ]() {
            const int index = findVisibilityIndex(
                visibilityModel_,
                FilteredView::Visibility( LogFilteredData::VisibilityFlags::Matches ) );
            if ( index >= 0 ) {
                visibilityBox_->setCurrentIndex( index );
            }
        } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerIncreseTopViewSize, [ this ]() { changeTopViewSize( 1 ); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerDecreaseTopViewSize, [ this ]() { changeTopViewSize( -1 ); } );

    const auto exitSearchKeySequence = QKeySequence( QKeySequence::Cancel );
    ShortcutAction::registerShortcut( exitSearchKeySequence.toString(), shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut, [ this ]() {
                                          const auto activeView = this->activeView();
                                          if ( activeView ) {
                                              activeView->setFocus();
                                          }
                                      } );

    std::array<std::string, 10> colorLables = {
        ShortcutAction::LogViewAddColorLabel1, ShortcutAction::LogViewAddColorLabel2,
        ShortcutAction::LogViewAddColorLabel3, ShortcutAction::LogViewAddColorLabel4,
        ShortcutAction::LogViewAddColorLabel5, ShortcutAction::LogViewAddColorLabel6,
        ShortcutAction::LogViewAddColorLabel7, ShortcutAction::LogViewAddColorLabel8,
        ShortcutAction::LogViewAddColorLabel9, ShortcutAction::LogViewAddColorLabel10,
    };

    for ( auto label = 0u; label < colorLables.size(); ++label ) {
        ShortcutAction::registerShortcut(
            configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
            colorLables[ label ], [ this, label ]() { addColorLabelToSelection( label ); } );
    }

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::LogViewAddNextColorLabel, [ this ]() { addNextColorLabelToSelection(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::LogViewClearColorLabels, [ this ]() { clearColorLabels(); } );

    logMainView_->registerShortcuts();
    filteredView_->registerShortcuts();
}

void CrawlerWidget::loadIcons()
{
    const auto& config = Configuration::get();
    const auto palette = config.themePalette( config.theme() );
    auto checkedColor = palette.count( "ToggleCheckedText" ) > 0
                            ? QColor( palette.at( "ToggleCheckedText" ) )
                            : QColor();
    if ( !checkedColor.isValid() && palette.count( "HighlightedText" ) > 0 ) {
        checkedColor = QColor( palette.at( "HighlightedText" ) );
    }

    searchRefreshButton_->setIcon(
        iconLoader_.loadChecked( "icons8-search-refresh", checkedColor ) );
    useRegexpButton_->setIcon( iconLoader_.loadChecked( "regex", checkedColor ) );
    inverseButton_->setIcon( iconLoader_.loadChecked( "icons8-not-equal", checkedColor ) );
    booleanButton_->setIcon( iconLoader_.loadChecked( "icons8-venn-diagram", checkedColor ) );
    clearButton_->setIcon( iconLoader_.load( "icons8-delete" ) );
    searchButton_->setIcon( iconLoader_.load( "icons8-search" ) );
    keepSearchResultsButton_->setIcon( iconLoader_.load( "icons8-lock" ) );
    matchCaseButton_->setIcon( iconLoader_.loadChecked( "icons8-font-size", checkedColor ) );
    stopButton_->setIcon( iconLoader_.load( "icons8-close-window" ) );
}

void CrawlerWidget::applyToggleButtonPaletteStyle()
{
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

    const auto style = QString( "QToolButton:checked { color: %1; background-color: %2; }" )
                           .arg( checkedColor.name( QColor::HexArgb ) )
                           .arg( checkedBackground.name( QColor::HexArgb ) );

    for ( auto* button : { matchCaseButton_, useRegexpButton_, inverseButton_, booleanButton_,
                           searchRefreshButton_ } ) {
        if ( button != nullptr ) {
            button->setStyleSheet( style );
        }
    }
}

// Create a new search using the text passed, replace the currently
// used one and destroy the old one.
void CrawlerWidget::replaceCurrentSearch( const QString& searchText )
{
    runUserSearch( searchText );
}

void CrawlerWidget::runUserSearch( const QString& searchText )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::runUserSearch" );
    LOG_INFO << "replacing current search with " << searchText;

    auto& apiSession = klogg::ui::apiSession();
    auto& viewData = currentViewData();

    // Cancel the tab's ongoing API search; its late progress is ignored by
    // the id check in the marshalled callback
    cancelViewSearch( viewData );

    nbMatches_ = 0_lcount;

    // User search updates Matches only; Events are independent data.

    if ( !searchText.isEmpty() ) {
        const auto chipPatterns = patternInputWidget_->chips();
        const auto shouldUseBooleanCombining
            = booleanButton_->isChecked()
              || ( patternInputWidget_->isChipMode() && !useRegexpButton_->isChecked()
                   && chipPatterns.size() > 1 );

        // Constructs the regexp
        auto regexpPattern = RegularExpressionPattern(
            searchText.toStdString(), matchCaseButton_->isChecked(), inverseButton_->isChecked(),
            shouldUseBooleanCombining, !useRegexpButton_->isChecked() );

        RegularExpression hsExpression{ regexpPattern };
        auto isValidExpression = hsExpression.isValid();

        if ( isValidExpression ) {
            // Activate the stop button
            stopButton_->setEnabled( true );
            stopButton_->show();
            clearButton_->hide();
            searchButton_->hide();

            klogg::api::SearchParams params;
            params.pattern = searchText.toStdString();
            params.caseSensitive = matchCaseButton_->isChecked();
            params.isRegex = useRegexpButton_->isChecked();
            params.invertMatch = inverseButton_->isChecked();
            params.isBoolean = shouldUseBooleanCombining;
            // The engine endLine is exclusive, same as searchEndLine_
            params.fromLine = searchStartLine_;
            params.toLine = searchEndLine_;

            const auto search = apiSession.runSearch( fileId_, params );
            if ( search ) {
                adoptSearchData( *search );
                // Accept auto-refresh of the search
                searchState_.startSearch();
                searchInfoLine_->hide();
            }
            else {
                LOG_ERROR << "API runSearch failed: " << search.error().message;
                switchToEmptyData();
                searchState_.resetState();
            }

            // For boolean expressions, highlight only the include terms.
            // AbstractLogView skips match highlighting when isBoolean=true.
            if ( shouldUseBooleanCombining && patternInputWidget_->isChipMode() ) {
                const auto highlightExpr = patternInputWidget_->includeRegex();
                if ( !highlightExpr.isEmpty() ) {
                    auto highlightPattern = RegularExpressionPattern(
                        highlightExpr.toStdString(), matchCaseButton_->isChecked(), false, false,
                        false );
                    logMainView_->setSearchPattern( highlightPattern );
                    filteredView_->setSearchPattern( highlightPattern );
                }
                else {
                    logMainView_->setSearchPattern( regexpPattern );
                    filteredView_->setSearchPattern( regexpPattern );
                }
            }
            else {
                logMainView_->setSearchPattern( regexpPattern );
                filteredView_->setSearchPattern( regexpPattern );
            }
        }
        else {
            // The regexp is wrong: render the empty (marks-only) data
            switchToEmptyData();
            searchState_.resetState();

            // Inform the user
            QString errorString = QString::fromStdString( hsExpression.errorString() );
            QString errorMessage = tr( "Error in expression" );
            // const int offset = regexp.patternErrorOffset();
            // if ( offset != -1 ) {
            //     errorMessage += " at position ";
            //     errorMessage += QString::number( offset );
            // }
            errorMessage += ": ";
            errorMessage += errorString;
            searchInfoLine_->setPalette( ErrorPalette );
            searchInfoLine_->setText( errorMessage );
            searchInfoLine_->show();

            logMainView_->setSearchPattern( {} );
            filteredView_->setSearchPattern( {} );
        }
    }
    else {
        switchToEmptyData();
        searchState_.resetState();
        printSearchInfoMessage();
    }
}

// Updates the content of the drop down list for the saved searches,
// called when the SavedSearch has been changed.
void CrawlerWidget::updateSearchCombo()
{
    const QString text = searchLineEdit_->lineEdit()->text();
    searchLineEdit_->clear();

    auto searchHistory = searchHistoryItems();

    searchLineEdit_->addItems( searchHistory );
    // In case we had something that wasn't added to the list (blank...):
    searchLineEdit_->lineEdit()->setText( text );

    searchLineCompleter_->setModel( new QStringListModel( searchHistory, searchLineCompleter_ ) );
}

// Print the search info message.
void CrawlerWidget::printSearchInfoMessage( LinesCount nbMatches )
{
    QString text;

    switch ( searchState_.getState() ) {
    case SearchState::NoSearch:
        // Blank text is fine
        break;
    case SearchState::Static:
    case SearchState::Autorefreshing:
        // Some languages translate the plural the same as the singular, so use the full string
        text = nbMatches.get() > 1 ? tr( "%1 matches found" ).arg( nbMatches.get() )
                                   : tr( "%1 match found" ).arg( nbMatches.get() );
        break;
    case SearchState::FileTruncated:
    case SearchState::TruncatedAutorefreshing:
        text = tr( "File truncated on disk" );
        break;
    }

    searchInfoLine_->setPalette( searchInfoLineDefaultPalette_ );
    searchInfoLine_->setText( text );
    searchInfoLine_->setVisible( !text.isEmpty() );
}

// Change the data status and, if needed, advise upstream.
void CrawlerWidget::changeDataStatus( DataStatus status )
{
    if ( ( status != dataStatus_ )
         && ( !( dataStatus_ == DataStatus::NEW_FILTERED_DATA
                 && status == DataStatus::NEW_DATA ) ) ) {
        dataStatus_ = status;
        Q_EMIT dataStatusChanged( dataStatus_ );
    }
}

// Determine the right encoding and set the views.
void CrawlerWidget::updateEncoding()
{
    const QTextCodec* textCodec = [ this ]() {
        QTextCodec* codec = nullptr;
        if ( !encodingMib_ ) {
            // The API fileInfo tracks the live detected codec
            const auto info = klogg::ui::apiSession().fileInfo( fileId_ );
            if ( info ) {
                codec = QTextCodec::codecForName( info->encoding.name.c_str() );
            }
            else {
                LOG_WARNING << "CrawlerWidget: fileInfo failed: " << info.error().message;
            }
        }
        else {
            codec = QTextCodec::codecForMib( *encodingMib_ );
        }
        return codec ? codec : QTextCodec::codecForLocale();
    }();

    QString encodingPrefix = encodingMib_ ? tr( "Displayed as %1" ) : tr( "Detected as %1" );
    encodingText_ = encodingPrefix.arg( textCodec->name().constData() );

    auto& session = klogg::ui::apiSession();
    const auto interrupted = session.interruptLoading( fileId_ );
    if ( !interrupted ) {
        LOG_WARNING << "interruptLoading failed: " << interrupted.error().message;
    }
    // Returns once the codec is applied; a mib change reindexes
    // asynchronously (completion arrives through the session event route as
    // handleIndexingFinished)
    const auto applied = session.setEncodingAsync( fileId_, textCodec->name().constData(), {} );
    if ( !applied ) {
        LOG_WARNING << "setEncodingAsync failed: " << applied.error().message;
    }

    logMainView_->forceRefresh();
    // LogFilteredData inherits the display codec from its LogData, so only
    // the repaint is needed here
    filteredView_->forceRefresh();
}

// Change the respective size of the two views
void CrawlerWidget::changeTopViewSize( int32_t delta )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::changeTopViewSize" );
    int min, max;
    getRange( 1, &min, &max );
    LOG_DEBUG << "CrawlerWidget::changeTopViewSize " << sizes().at( 0 ) << " " << min << " " << max;
    moveSplitter( closestLegalPosition( sizes().at( 0 ) + ( delta * 10 ), 1 ), 1 );
    LOG_DEBUG << "CrawlerWidget::changeTopViewSize " << sizes().at( 0 );
}

void CrawlerWidget::addColorLabelToSelection( size_t label )
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::addColorLabelToSelection" );
    updateColorLabels( colorLabelsManager_.setColorLabel( label, getSelectedText() ) );
}

void CrawlerWidget::addNextColorLabelToSelection()
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::addNextColorLabelToSelection" );
    updateColorLabels( colorLabelsManager_.setNextColorLabel( getSelectedText() ) );
}

void CrawlerWidget::clearColorLabels()
{
    KLOGG_UI_TRACE_SCOPE( "CrawlerWidget::clearColorLabels" );
    updateColorLabels( colorLabelsManager_.clear() );
}

void CrawlerWidget::updateColorLabels(
    const ColorLabelsManager::QuickHighlightersCollection& labels )
{
    logMainView_->setQuickHighlighters( labels );
    filteredView_->setQuickHighlighters( labels );
}

//
// SearchState implementation
//
void CrawlerWidget::SearchState::resetState()
{
    state_ = NoSearch;
}

void CrawlerWidget::SearchState::setAutorefresh( bool refresh )
{
    autoRefreshRequested_ = refresh;

    if ( refresh ) {
        if ( state_ == Static )
            state_ = Autorefreshing;
        /*
        else if ( state_ == FileTruncated )
            state_ = TruncatedAutorefreshing;
        */
    }
    else {
        if ( state_ == Autorefreshing )
            state_ = Static;
        else if ( state_ == TruncatedAutorefreshing )
            state_ = FileTruncated;
    }
}

void CrawlerWidget::SearchState::truncateFile()
{
    if ( state_ == Autorefreshing || state_ == TruncatedAutorefreshing ) {
        state_ = TruncatedAutorefreshing;
    }
    else {
        state_ = FileTruncated;
    }
}

void CrawlerWidget::SearchState::changeExpression()
{
    if ( state_ == Autorefreshing )
        state_ = Static;
}

void CrawlerWidget::SearchState::stopSearch()
{
    if ( state_ == Autorefreshing )
        state_ = Static;
}

void CrawlerWidget::SearchState::startSearch()
{
    if ( autoRefreshRequested_ )
        state_ = Autorefreshing;
    else
        state_ = Static;
}

/*
 * CrawlerWidgetContext
 */
CrawlerWidgetContext::CrawlerWidgetContext( const QString& string )
{
    if ( string.startsWith( '{' ) ) {
        loadFromJson( string );
    }
    else {
        loadFromString( string );
    }
}

void CrawlerWidgetContext::loadFromString( const QString& string )
{
    QRegularExpression regex( "S(\\d+):(\\d+)" );
    QRegularExpressionMatch match = regex.match( string );
    if ( match.hasMatch() ) {
        sizes_ = { match.captured( 1 ).toInt(), match.captured( 2 ).toInt() };
        LOG_DEBUG << "sizes_: " << sizes_[ 0 ] << " " << sizes_[ 1 ];
    }
    else {
        LOG_WARNING << "Unrecognised view size: " << string.toLocal8Bit().data();

        // Default values;
        sizes_ = { 400, 100 };
    }

    QRegularExpression case_refresh_regex( "IC(\\d+):AR(\\d+)" );
    match = case_refresh_regex.match( string );
    if ( match.hasMatch() ) {
        ignoreCase_ = ( match.captured( 1 ).toInt() == 1 );
        autoRefresh_ = ( match.captured( 2 ).toInt() == 1 );

        LOG_DEBUG << "ignore_case_: " << ignoreCase_ << " auto_refresh_: " << autoRefresh_;
    }
    else {
        LOG_WARNING << "Unrecognised case/refresh: " << string.toLocal8Bit().data();
        ignoreCase_ = false;
        autoRefresh_ = false;
    }

    QRegularExpression follow_regex( "AR(\\d+):FF(\\d+)" );
    match = follow_regex.match( string );
    if ( match.hasMatch() ) {
        followFile_ = ( match.captured( 2 ).toInt() == 1 );

        LOG_DEBUG << "follow_file_: " << followFile_;
    }
    else {
        LOG_WARNING << "Unrecognised follow file " << string.toLocal8Bit().data();
        followFile_ = false;
    }

    useRegexp_ = Configuration::get().mainRegexpType() == SearchRegexpType::ExtendedRegexp;
}

void CrawlerWidgetContext::loadFromJson( const QString& json )
{
    const auto properties = QJsonDocument::fromJson( json.toLatin1() ).toVariant().toMap();

    if ( properties.contains( "S" ) ) {
        const auto sizes = properties.value( "S" ).toList();
        for ( const auto& s : sizes ) {
            sizes_.append( s.toInt() );
        }
    }

    ignoreCase_ = properties.value( "IC" ).toBool();
    autoRefresh_ = properties.value( "AR" ).toBool();
    followFile_ = properties.value( "FF" ).toBool();
    if ( properties.contains( "RE" ) ) {
        useRegexp_ = properties.value( "RE" ).toBool();
    }
    else {
        useRegexp_ = Configuration::get().mainRegexpType() == SearchRegexpType::ExtendedRegexp;
    }

    if ( properties.contains( "IR" ) ) {
        inverseRegexp_ = properties.value( "IR" ).toBool();
    }
    else {
        inverseRegexp_ = false;
    }

    if ( properties.contains( "BC" ) ) {
        useBooleanCombination_ = properties.value( "BC" ).toBool();
    }
    else {
        useBooleanCombination_ = false;
    }

    if ( properties.contains( "M" ) ) {
        const auto marks = properties.value( "M" ).toList();
        for ( const auto& m : marks ) {
            marks_.append( m.toUInt() );
        }
    }
}

QString CrawlerWidgetContext::toString() const
{
    const auto toVariantList = []( const auto& list ) -> QVariantList {
        QVariantList variantList;
        for ( const auto& item : list ) {
            variantList.append( static_cast<qulonglong>( item ) );
        }
        return variantList;
    };

    QVariantMap properies;

    properies[ "S" ] = toVariantList( sizes_ );
    properies[ "IC" ] = ignoreCase_;
    properies[ "AR" ] = autoRefresh_;
    properies[ "FF" ] = followFile_;
    properies[ "RE" ] = useRegexp_;
    properies[ "IR" ] = inverseRegexp_;
    properies[ "BC" ] = useBooleanCombination_;
    properies[ "M" ] = toVariantList( marks_ );

    return QJsonDocument::fromVariant( properies ).toJson( QJsonDocument::Compact );
}
