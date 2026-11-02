/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014, 2015 Nicolas Bonnefon
 * and other contributors
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

#ifndef CRAWLERWIDGET_H
#define CRAWLERWIDGET_H

#include <cstddef>
#include <optional>

#include <QCheckBox>
#include <QComboBox>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QMenu>
#include <QPushButton>
#include <QRect>
#include <QRegularExpression>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include "colorlabelsmanager.h"
#include "filteredview.h"
#include "iconloader.h"
#include "linetypes_qt.h"
#include "loadingstatus.h"
#include "logmainview.h"
#include "overview.h"
#include "patterninputwidget.h"
#include "predefinedfilterscombobox.h"
#include "signalmux.h"
#include "viewinterface.h"

#include "klogg/api_events.h"
#include "klogg/api_types.h"

class InfoLine;
class QuickFindPattern;
class QStandardItemModel;
class QCompleter;
class OverviewWidget;

// Implements the central widget of the application.
// It includes both windows, the search line, the info
// lines and various buttons.
class CrawlerWidget : public QSplitter,
                      public QuickFindMuxSelectorInterface,
                      public ViewInterface,
                      public MuxableDocumentInterface {
    Q_OBJECT

  public:
    CrawlerWidget( QWidget* parent = nullptr );
    ~CrawlerWidget() override;

    // Get the line number of the first line displayed.
    LineNumber getTopLine() const;
    // Get the selected text as a string (from the main window)
    QString getSelectedText() const;
    void copyWithColor() const;
    // True for partial selection
    bool isPartialSelection() const;

    // Display the QFB at the bottom, remembering where the focus was
    void displayQuickFindBar( QuickFindMux::QFDirection direction );

    // Instructs the widget to select all the text in the window the user
    // is interacting with
    void selectAll();

    std::optional<int> encodingMib() const;

    // Get the text description of the encoding effectively used,
    // suitable to display to the user.
    QString encodingText() const;

    // Returns whether follow is enabled in this crawler
    bool isFollowEnabled() const;

    bool isTextWrapEnabled() const;
    int rightSideReservedWidth() const;
    QRect sliceOverlayAnchorRect() const;

    bool isChipMode() const;

    QString getCurrentSearchText() const;

    void scrollToLine( LineNumber line );

    void setSearchText( const QString& text );

    void registerShortcuts();

    // Perform event extraction through the Core API and mirror the results
    // into the current data set; with a completed extraction at hand the
    // events are re-applied without re-scanning (e.g. category change)
    void performEventSearch();
    void updateEventView();
    void setCurrentEventCategories(const QStringList& categories);
    static int findVisibilityIndex(const QStandardItemModel* model, FilteredView::Visibility visibility);
    QVector<SliceLineRange> slicesAtSourceLine(LineNumber line) const;

    // Identity of the file in the Core API session
    klogg::api::FileId fileId() const
    {
        return fileId_;
    }
    // The crawler's latest extraction (running or completed), if any
    std::optional<klogg::api::ExtractionId> currentEventExtractionId() const
    {
        return eventExtractionId_;
    }
    // Drops the cached extraction (event rules changed); the next event view
    // refresh starts a fresh one
    void invalidateEventExtraction();
    // Entry points of the process-wide session event dispatch, routed here by
    // extraction id; both arrive on the GUI thread
    void handleEventExtractionProgress( klogg::api::ExtractionId extractionId, double progress,
                                        uint32_t eventsFound );
    void handleEventExtractionCompleted( klogg::api::ExtractionId extractionId,
                                         uint32_t eventCount );

    // Entry points of the per-file session event dispatch, routed here by
    // file id; all arrive on the GUI thread
    void handleIndexingProgress( int progress );
    void handleIndexingFinished( LoadingStatus status );
    void handleFileChanged( MonitoredFileStatus status );
    // Session-level marks changed through another consumer of this file
    // (e.g. a second window); refreshes the local mark navigation caches
    void handleMarksChanged();

    // Check if current visibility includes Events
    bool isEventVisibility() const;

  public Q_SLOTS:
    // Stop the asynchoronous loading of the file if one is in progress
    // The file is identified by the view attached to it.
    void stopLoading();
    // Reload the displayed file
    void reload();
    // Set the encoding
    void setEncoding( std::optional<int> mib );

    void focusSearchEdit();
    void goToLine();
    void jumpToTop();

    // Instructs the widget to reconfigure itself because Config() has changed.
    void applyConfiguration();

    void setChipMode( bool enabled );

  public:
    template <class T>
    struct access_by;

  protected:
    // Implementation of the ViewInterface functions
      void doSetData( klogg::api::FileId fileId ) override;
      void doSetQuickFindPattern( std::shared_ptr<QuickFindPattern> qfp ) override;
      void doSetViewContext( const QString& viewContext ) override;
      std::shared_ptr<const ViewContextInterface> doGetViewContext( void ) const override;
      void doReplayIndexingFinished() override;

      // Implementation of the mux selector interface
      // (for dispatching QuickFind to the right widget)
      SearchableWidgetInterface* doGetActiveSearchable() const override;
      std::vector<QObject*> doGetAllSearchables() const override;

      // Implementation of the MuxableDocumentInterface
      void doSendAllStateSignals() override;

      void changeEvent( QEvent* event ) override;

  Q_SIGNALS:
    // Sent to signal the client load has progressed,
    // passing the completion percentage.
    void loadingProgressed( int progress );
    // Sent to the client when the loading has finished
    // whether successful or not.
    void loadingFinished( LoadingStatus status );
    // Sent when follow mode is enabled/disabled
    void followSet( bool checked );
    // Sent when text wrap mode is enabled/disabled
    void textWrapSet( bool checked );
    // Sent when chip mode is enabled/disabled
    void chipModeSet( bool checked );
    // Sent up to the MainWindow to enable/disable the follow mode
    void followModeChanged( bool follow );
    // Sent up when the current line number is updated
    void newSelection( LineNumber startLine, LinesCount nLines, LineColumn startCol,
                       LineLength nSymbols );
    // Sent up when user wants to save new predefined filter from current search
    void saveCurrentSearchAsPredefinedFilter( QString newFilter, bool useRegex );

    void sendToScratchpad( QString );
    void replaceDataInScratchpad( QString );

    // "auto-refresh" check has been changed
    void searchRefreshChanged( bool isRefreshing );
    // "ignore case" check has been changed
    void matchCaseChanged( bool matchCase );

    // Sent when the data status (whether new not seen data are
    // available) has changed
    void dataStatusChanged( DataStatus status );

    // Sent up when the current filtered view has been changed
    void filteredViewChanged();
    
    // Sent when Events visibility is requested
    void eventVisibilityRequested();
    // Sent when filtered view visibility changes (includes Events flag)
    void visibilityChanged( bool includesEvents );
    // Sent when event rules are changed
    void eventRulesChanged();
    // Sent when selected source line changes for slice overlay
    void selectedSourceLineChanged( LineNumber line );

  private Q_SLOTS:
    // Instructs the widget to start a search using the current search line.
    void startNewSearch();
    // Stop the currently ongoing search (if one exists)
    void stopSearch();
    void loadIcons();
    // QuickFind is being entered, save the focus for incremental qf.
    void enteringQuickFind();
    // QuickFind is being closed.
    void exitingQuickFind();
    // Called when new Matches data must be displayed in the filtered window.
    void updateMatchesFilteredView( LinesCount nbMatches, int progress, LineNumber initialPosition );
    // Called when new Events data must be displayed in the filtered window.
    void updateEventsFilteredView( LinesCount nbEvents, int progress, LineNumber initialPosition );
    // Called when a new line has been selected in the filtered view,
    // to instruct the main view to jump to the matching line.
    void jumpToMatchingLine( LineNumber filteredLineNb, LinesCount nLines, LineColumn startCol,
                             LineLength nSymbols );
    // Called when the main view is on a new line number
    void updateLineNumberHandler( LineNumber line, LinesCount nLines, LineColumn startCol,
                                  LineLength nSymbols );
    // Mark a line that has been clicked on the main (top) view.
    void markLinesFromMain( const klogg::vector<LineNumber>& lines );
    // Mark a line that has been clicked on the filtered (bottom) view.
    void markLinesFromFiltered( const klogg::vector<LineNumber>& lines );

    void loadingFinishedHandler( LoadingStatus status );
    // Manages the info lines to inform the user the file has changed.
    void fileChangedHandler( MonitoredFileStatus );

    void searchForward();
    void searchBackward();

    // Called when the checkbox for search auto-refresh is changed
    void searchRefreshChangedHandler( bool isRefreshing );

    // Called when the checkbox for case sensitivity is changed
    void matchCaseChangedHandler( bool shouldMatchCase );

    // Called when the checkbox for boolean combining is changed
    void booleanCombiningChangedHandler( bool shouldCombine );

    // Called when the checkbox for using regex is changed
    void useRegexpChangeHandler( bool shouldUseRegex );

    // Called when the text on the search line is modified
    void searchTextChangeHandler( QString );

    // Called when the user change the visibility combobox
    void changeFilteredViewVisibility( int index );

    // Called when the user add the string to the search
    void addToSearch( const QString& string );

    // Called when the user replaces the search with the selection string
    void replaceSearch( const QString& string );

    // Called when the user excludes selection string from search
    // works only in boolean combination mode
    void excludeFromSearch( const QString& string );

    void clearSearchHistory();
    void editSearchHistory();

    // Save current search as predefined filter
    void saveAsPredefinedFilter();
    void setSearchPatternFromPredefinedFilters( const klogg::vector<PredefinedFilter>& filters );

    // Search Context Menu
    void showSearchContextMenu();

    // Called when a match is hovered on in the filtered view
    void mouseHoveredOverMatch( LineNumber line );

    // Called when there was activity in the views
    void activityDetected();

    void setSearchLimits( LineNumber startLine, LineNumber endLine );
    void clearSearchLimits();

    void addColorLabelToSelection( size_t label );
    void addNextColorLabelToSelection();
    void clearColorLabels();

    void changeFilteredView(int tabIndex);
    void closeFilteredView(int tabIndex);
    void filteredViewDestroyed(QObject* view);
    void handleMainViewSelectedLine( LineNumber line, LinesCount nLines, LineColumn startCol,
                                     LineLength nSymbols );
    void handleFilteredViewSelectedLine( LineNumber line, LinesCount nLines, LineColumn startCol,
                                         LineLength nSymbols );

  private:
    // State machine holding the state of the search, used to allow/disallow
    // auto-refresh and inform the user via the info line.
    class SearchState {
      public:
        enum State {
            NoSearch,
            Static,
            Autorefreshing,
            FileTruncated,
            TruncatedAutorefreshing,
        };

        SearchState()
        {
            state_ = NoSearch;
            autoRefreshRequested_ = false;
        }

        // Reset the state (no search active)
        void resetState();
        // The user changed auto-refresh request
        void setAutorefresh( bool refresh );
        // The file has been truncated (stops auto-refresh)
        void truncateFile();
        // The expression has been changed (stops auto-refresh)
        void changeExpression();
        // The search has been stopped (stops auto-refresh)
        void stopSearch();
        // The search has been started (enable auto-refresh)
        void startSearch();

        // Get the state in order to display the proper message
        State getState() const
        {
            return state_;
        }
        // Is auto-refresh allowed
        bool isAutorefreshAllowed() const
        {
            return ( state_ == Autorefreshing || state_ == TruncatedAutorefreshing );
        }
        bool isFileTruncated() const
        {
            return ( state_ == FileTruncated || state_ == TruncatedAutorefreshing );
        }

      private:
        State state_;
        bool autoRefreshRequested_;
    };

    // Private functions
    void setup();
    void setShortcuts();
    void runUserSearch( const QString& searchText );
    void replaceCurrentSearch( const QString& searchText );
    void updateSearchCombo();
    AbstractLogView* activeView() const;
    void printSearchInfoMessage( LinesCount nbMatches = 0_lcount );
    // Match count of the tab's live API search; no active search means none
    LinesCount currentMatchCount() const;
    // Re-reads total_lines from the API session fileInfo into the cache;
    // a failed read keeps the previous value
    void refreshTotalLines();
    void changeDataStatus( DataStatus status );
    void updateEncoding();
    void changeTopViewSize( int32_t delta );
    void updatePredefinedFiltersWidget();

    // Reload predefined filters after changing settings
    void reloadPredefinedFilters() const;

    QString escapeSearchPattern( const QString& searchPattern, bool isRegex = false ) const;
    QString& combinePatterns( QString& currentPattern, const QString& newPattern ) const;
    void setSearchPattern( const QString& searchPattern );

    void resetStateOnSearchPatternChanges();

    void updateColorLabels( const ColorLabelsManager::QuickHighlightersCollection& labels );

    void connectAllFilteredViewSlots( FilteredView* view);

    // Per-tab state: the search bound to the tab (nullopt renders the
    // search-less empty projection)
    struct FilteredViewData {
        std::optional<klogg::api::SearchId> searchId;
    };

    // Tab bookkeeping for API-backed searches (B3)
    FilteredViewData& currentViewData();
    // Cancels the tab's API search and drops its reference to the result set
    void cancelViewSearch( FilteredViewData& viewData );
    // Stops the tab's API search but keeps the result set for rendering
    void interruptViewSearch( FilteredViewData& viewData );
    // Points the current tab's views at a fresh API search's projection
    void adoptSearchData( klogg::api::SearchId searchId );
    // Points the current tab's views back at the search-less empty projection
    void switchToEmptyData();
    // Session-level marks are the source of truth; every filtered tab's
    // mark navigation cache and the empty projection follow them here
    void mirrorMarksToCurrentData();
    // Rebuilds the search-less projection (visibility-filtered marks union
    // the populated extraction's event lines) and pushes it to every tab
    void rebuildEmptyProjection();

    // Source line -> union projection index with the native floor semantics
    // (greatest member at or before the line, clamped to 0); search-less
    // state floors over the empty projection
    LineNumber filteredIndexFloor( LineNumber sourceLine ) const;
    // Union projection index -> source line; maxValue when the index is
    // outside the projection (the native mapping's out-of-range value)
    LineNumber sourceLineForFilteredIndex( LineNumber index ) const;
    // Event tooltip lookups backing the views' providers; the filtered
    // variant maps the projection index to a source line first
    QString eventTooltipAtSourceLine( LineNumber sourceLine ) const;
    QString eventTooltipAtFilteredLine( LineNumber viewLine ) const;

    void saveSplitterSizes() const;
    void clearSliceRangeIndex();
    // Pulls the current extraction's events and slice ranges off the GUI
    // thread, then updates the per-tab UI state (tooltips, overlay, empty
    // projection)
    void populateEventsFromExtraction();
    void registerEventRoute( klogg::api::ExtractionId extractionId );
    void unregisterEventRoute( klogg::api::ExtractionId extractionId );
    // Per-file route for the indexing lifecycle events; several crawlers may
    // share a file ("open in new window")
    void registerFileRoute();
    void unregisterFileRoute();
    void emitSelectedSourceLineChanged( LineNumber line );

    void changeFontSize( bool increase );

    // Palette for error notification (yellow background)
    static const QPalette ErrorPalette;

    IconLoader iconLoader_;

    // Sorted source lines backing the search-less filtered view (marks union
    // the populated extraction's event lines, visibility-filtered)
    std::vector<LineNumber> emptyProjectionLines_;

    // Identity of the file in the Core API session; everything is read
    // through it
    klogg::api::FileId fileId_ = 0;
    // total_lines cached from the API fileInfo; refreshed on data changes so
    // hot paths avoid a session round-trip per read
    LinesCount cachedTotalLines_ = 0_lcount;
    // Search bound to the current tab's filtered view
    std::optional<klogg::api::SearchId> currentSearchId_;

    // Matches overview
    Overview overview_;

    std::shared_ptr<QuickFindPattern> quickFindPattern_;

    LogMainView* logMainView_;
    FilteredView* filteredView_;
    std::unordered_map<FilteredView*, FilteredViewData> filteredViewsData_;
    QTabWidget* tabbedFilteredView_;

    OverviewWidget* overviewWidget_;

    QComboBox* visibilityBox_;
    QStandardItemModel* visibilityModel_;

    PredefinedFiltersComboBox* predefinedFilters_;

    QComboBox* searchLineEdit_;
    PatternInputWidget* patternInputWidget_;
    QMenu* searchLineContextMenu_;
    QAction* saveAsPredefinedFilterAction_;
    QAction* editSearchHistoryAction_;
    QAction* clearSearchHistoryAction_;
    QCompleter* searchLineCompleter_;
    QMetaObject::Connection chipHistoryConnection_;

    InfoLine* searchInfoLine_;

    QToolButton* clearButton_;
    QToolButton* searchButton_;
    QToolButton* keepSearchResultsButton_;
    QToolButton* stopButton_;

    QToolButton* matchCaseButton_;
    QToolButton* useRegexpButton_;
    QToolButton* inverseButton_;
    QToolButton* booleanButton_;
    QToolButton* searchRefreshButton_;
    QToolButton* chipModeButton_ = nullptr;

    bool booleanAutoEnabled_ = false;
    QString lastChipExpression_;

    std::map<QString, QShortcut*> shortcuts_;

    // Default palette to be remembered
    QPalette searchInfoLineDefaultPalette_;

    // Reference to the QuickFind Pattern (not owned)

    QWidget* qfSavedFocus_ = nullptr;

    // Search state (for auto-refresh and truncation)
    SearchState searchState_;

    // the current dataStatus (whether we have new, not seen, data)
    DataStatus dataStatus_ = DataStatus::OLD_DATA;

    // Last main line number received
    LineNumber currentLineNumber_;

    void applyToggleButtonPaletteStyle();

    // Current number of matches
    LinesCount nbMatches_;

    LineNumber searchStartLine_;
    LineNumber searchEndLine_;

    // Until we have received confirmation loading is finished, we
    // should consider we are loading something.
    bool loadingInProgress_ = true;
    bool firstLoadDone_ = false;

    klogg::vector<LineNumber> savedMarkedLines_;
    
    // Current event category filter (empty means "All Categories")
    QStringList currentEventCategories_;

    // Latest event extraction started for this file (via the Core API);
    // progress/completion arrive through the session event subscription
    std::optional<klogg::api::ExtractionId> eventExtractionId_;
    // Extraction whose events are currently mirrored into the rendered set
    std::optional<klogg::api::ExtractionId> populatedExtractionId_;
    // Dropped populate results carry an older generation
    quint64 eventPopulateGeneration_ = 0;

    QVector<SliceLineRange> sliceRanges_;
    QHash<qulonglong, QVector<int>> sliceStartsByLine_;
    QHash<qulonglong, QVector<int>> sliceEndsByLine_;
    mutable QVector<int> activeSliceIndicesCache_;
    mutable bool activeSliceCacheValid_ = false;
    mutable LineNumber activeSliceCacheLine_ = 0_lnum;
    std::optional<LineNumber> selectedSourceLine_;
    // Tooltip text per event line of the populated extraction, keyed by
    // source line; the views query it through their tooltip provider
    QMap<LineNumber, QString> eventTooltipsByLine_;

    // Current encoding setting;
    std::optional<int> encodingMib_;
    QString encodingText_;

    ColorLabelsManager colorLabelsManager_;
};

#endif
