/*
 * Copyright (C) 2009, 2010, 2011, 2012, 2013, 2017 Nicolas Bonnefon
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
 * Copyright (C) 2016 -- 2021 Anton Filimonov and other contributors
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

#ifndef ABSTRACTLOGVIEW_H
#define ABSTRACTLOGVIEW_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <qchar.h>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <QAbstractScrollArea>
#include <QBasicTimer>
#include <QColor>
#include <QEvent>
#include <QFontMetrics>
#include <QString>
#include <QTextCodec>

#ifdef GLOGG_PERF_MEASURE_FPS
#include "perfcounter.h"
#endif

#include "abstractlogdata.h"
#include "ansi_parser.h"
#include "eventconfigdialog.h"
#include "highlightedmatch.h"
#include "highlighterset.h"
#include "klogg/api_render.h"
#include "linedatasource.h"
#include "linetypes_qt.h"
#include "overviewwidget.h"
#include "quickfind.h"
#include "quickfindmux.h"
#include "regularexpressionpattern.h"
#include "selection.h"
#include "viewtools.h"

class QMenu;
class QAction;
class QShortcut;
class HighlightersMenu;

namespace klogg::ui {

// Paint path selector. The new path composes the whole visible frame through
// the Core API session (Session::renderViewport); the old one composes every
// line inside the view, as it did before. The new path is the default, and
// KLOGG_RENDER_ENGINE=off restores the old one for a validation cycle, the
// same arrangement KLOGG_READ_CHANNEL uses in the api. The environment is read
// once, on first use.
bool renderEngineEnabled();

// Test hook: comparing the two paths pixel by pixel needs both of them in one
// process, which the environment cannot express. Production code never calls
// this.
void setRenderEngineOverride( std::optional<bool> enabled );

} // namespace klogg::ui

// Utility class representing a buffer for number entered on the keyboard
// The buffer keep at most 7 digits, and reset itself after a timeout.
class DigitsBuffer : public QObject {
    Q_OBJECT

public:
    // Reset the buffer.
    void reset();
    // Add a single digit to the buffer (discarded if it's not a digit),
    // the timeout timer is reset.
    void add( char character );
    // Get the content of the buffer (0 if empty) and reset it.
    LineNumber::UnderlyingType content();

    bool isEmpty() const;

protected:
    void timerEvent( QTimerEvent* event ) override;

private:
    // Duration of the timeout in milliseconds.
    static constexpr int DigitsTimeout = 2000;

    QString digits_;

    QBasicTimer timer_;
};

class Overview;

// Base class representing the log view widget.
// It can be either the top (full) or bottom (filtered) view.
class AbstractLogView : public QAbstractScrollArea,
                        public SearchableWidgetInterface,
                        public LineDataSource {
    Q_OBJECT

public:
    // Constructor of the widget.
    // The pointer to the QFP is used for colouring and QuickFind searches
    explicit AbstractLogView( const QuickFindPattern* const quickFind, QWidget* parent = nullptr );

    ~AbstractLogView() override;

    // rule of 5
    AbstractLogView( const AbstractLogView& ) = delete;
    AbstractLogView( AbstractLogView&& ) = delete;
    AbstractLogView& operator=( const AbstractLogView& ) = delete;
    AbstractLogView& operator=( AbstractLogView&& ) = delete;

    void updateFont( const QFont& font );

    // Refresh the widget when the data set has changed.
    // Virtual so views can refresh their line-source caches first (B4).
    virtual void updateData();
    // Instructs the widget to update it's content geometry,
    // used when the font is changed.
    void updateDisplaySize();
    // Return the line number of the top line of the view
    LineNumber getTopLine() const;
    // Return the text of the current selection.
    QString getSelectedText() const;
    void copyWithColor();
    // True for partial selection
    bool isPartialSelection() const;
    // Instructs the widget to select the whole text.
    void selectAll();

    bool isFollowEnabled() const
    {
        return followMode_;
    }

    bool isTextWrapEnabled() const
    {
        return useTextWrap_;
    }

    void allowFollowMode( bool allow );

    void setSearchPattern( const RegularExpressionPattern& pattern );

    // Per-tab event tooltip lookup, bound by the owning CrawlerWidget; the
    // view asks it on margin hover instead of reading the native data set
    void setEventTooltipProvider( std::function<QString( LineNumber )> provider )
    {
        eventTooltipProvider_ = std::move( provider );
    }

    using QuickHighlighters = QStringList;
    void setQuickHighlighters( const std::vector<QuickHighlighters>& wordHighlighters );

    void registerShortcuts();

    // LineDataSource: the base implementations return empty defaults;
    // LogMainView (B4) and FilteredView (B5b) override these to fetch
    // through the Core API session
    std::string getLineString( LineNumber line ) const override;
    std::string getExpandedLineString( LineNumber line ) const override;
    klogg::vector<std::string> getLines( LineNumber firstLine, LinesCount number ) const override;
    LineNumber getLineNumber( LineNumber index ) const override;
    LinesCount getNbLine() const override;
    LineLength getMaxLength() const override;
    LineLength getLineLength( LineNumber line ) const override;

protected:
    void mousePressEvent( QMouseEvent* mouseEvent ) override;
    void mouseMoveEvent( QMouseEvent* mouseEvent ) override;
    void mouseReleaseEvent( QMouseEvent* ) override;
    void mouseDoubleClickEvent( QMouseEvent* mouseEvent ) override;
    void timerEvent( QTimerEvent* timerEvent ) override;
    void changeEvent( QEvent* changeEvent ) override;
    void paintEvent( QPaintEvent* paintEvent ) override;
    void resizeEvent( QResizeEvent* resizeEvent ) override;
    void scrollContentsBy( int dx, int dy ) override;
    void keyPressEvent( QKeyEvent* keyEvent ) override;
    void wheelEvent( QWheelEvent* wheelEvent ) override;
    bool event( QEvent* e ) override;

    // Must be implemented to return what LineType the line number is
    // (used for coloured bullets)
    virtual AbstractLogData::LineType lineType( LineNumber lineNumber ) const = 0;

    // One frame of gutter flags for the paint loop (B5c): drawTextArea has
    // the view prefetch the visible range in one or two session round-trips
    // and the per-line lineType then reads this cache instead of
    // round-tripping per line; the filtered view's displayLineNumber also
    // reads its line-number projection from sourceLine. The cache is valid
    // only between the prefetch at the start of drawTextArea and the clear
    // at its end, so off-frame calls keep querying directly.
    struct GutterLineFlags {
        LineNumber sourceLine; // file line backing the view line
        uint8_t flagBits;      // klogg::api::LineFlag bits
    };

    // Fills the frame cache for [firstLine, firstLine + count) in view
    // lines; the default invalidates the cache (native per-line reads)
    virtual void prefetchGutterFlags( LineNumber firstLine, LinesCount count );
    void setGutterFlagsCache( LineNumber firstLine, klogg::vector<GutterLineFlags> flags );
    // Null when the cache is invalid or the line is outside the frame
    const GutterLineFlags* cachedGutterFlags( LineNumber lineNumber ) const;
    // Shared bit composition of the API-driven lineType paths
    static AbstractLogData::LineType lineTypeFromGutterFlags( uint8_t flagBits );

    // Line number to display for line at the given index
    virtual LineNumber displayLineNumber( LineNumber lineNumber ) const;
    virtual LineNumber lineIndex( LineNumber lineNumber ) const;
    virtual LineNumber maxDisplayLineNumber() const;

    // Composes the whole visible frame through the Core API session (the new
    // paint path): one call fetches the lines, their gutter flags and the
    // highlight state, and returns every row ready to paint. Views backed by
    // the session (LogMainView) override this; the base reports "unavailable"
    // so the paint loop keeps composing line by line. nullopt means the caller
    // falls back to that per-line path.
    virtual std::optional<klogg::api::ViewportResult> renderFrame( LineNumber firstLine,
                                                                   LinesCount count );

    // Everything about the frame this view owns: palette, wrap window, search
    // range, selection, pattern highlighters and QuickFind. The override above
    // builds its options with this, so both paths are driven by the same state.
    klogg::api::ViewportOptions viewportOptions( LineNumber firstLine, LinesCount count ) const;

    // Search range of the frame in view lines: start inclusive, end exclusive
    std::pair<LineNumber, LineNumber> frameSearchRange() const;

    // True when the main search pattern must be highlighted on the visible
    // lines: the setting is on and the search is a plain pattern search. Both
    // paint paths ask this, so they cannot disagree.
    bool hasPatternHighlight() const;

    // Scroll position mapping through the Core API session (Session::
    // scrollMultiplier / scrollPosToLine / lineToScrollPos). Only a view backed
    // by the session can answer these; the base reports "unavailable" and the
    // mapping then uses the widget's own line count, which is what the filtered
    // view needs (its line space is the search projection, not the file).
    // nullopt keeps the caller on that local path.
    virtual std::optional<double> sessionScrollMultiplier() const;
    virtual std::optional<int64_t> sessionScrollPosToLine( int64_t scrollPos ) const;
    virtual std::optional<int64_t> sessionLineToScrollPos( LineNumber line ) const;

    // Get the overview associated with this view, or NULL if there is none
    Overview* getOverview() const
    {
        return overview_;
    }
    // Set the Overview and OverviewWidget
    void setOverview( Overview* overview, OverviewWidget* overviewWidget );

    // Re-creates the QuickFind worker (its match-limit state is tied to the
    // data set): called when the view's data binding changes (FilteredView
    // swaps searches); the worker resolves lines through this view (B6)
    void restartQuickFind();

    // Display codec for saveToFile; subclasses with an API identity provide
    // the session-tracked one, the base reports none (utf-8 fallback)
    virtual const QTextCodec* displayEncoding() const;

    // Returns the current "position" of the view as a line number,
    // it is either the selected line or the middle of the view.
    LineNumber getViewPosition() const;

    virtual void doRegisterShortcuts();
    void registerShortcut( const std::string& action, std::function<void()> func );

Q_SIGNALS:
    // Sent up to the MainWindow to enable/disable the follow mode
    void followModeChanged( bool enabled );
    // Sent when the view wants the QuickFind widget pattern to change.
    void changeQuickFind( const QString& newPattern, QuickFindMux::QFDirection newDirection );
    // Sent when a new line has been selected by the user
    void newSelection( LineNumber startLine, LinesCount nLines, LineColumn startCol,
                       LineLength nSymbols );
    // Sent up when quickFind wants to show a message to the user.
    void notifyQuickFind( const QFNotification& message );
    // Sent up when quickFind wants to clear the notification.
    void clearQuickFindNotification();
    // Sent when the view ask for a line to be marked
    // (click in the left margin).
    void markLines( const klogg::vector<LineNumber>& lines );
    // Sent up when the user wants to add the selection to the search
    void addToSearch( const QString& selection );
    // Sent up when the user wants to replace the search with the selection
    void replaceSearch( const QString& selection );
    void excludeFromSearch( const QString& selection );
    // Sent when event rules are changed
    void eventRulesChanged();
    // Sent up when the mouse is hovered over a line's margin
    void mouseHoveredOverLine( LineNumber line );
    // Sent up when the mouse leaves a line's margin
    void mouseLeftHoveringZone();
    // Sent when mouse hovers a line in the content area
    void hoveredLineChanged( LineNumber line );
    // Sent when mouse leaves valid hovered content line area
    void hoveredLineLeft();
    // Sent up for view initiated quickfind searches
    void searchNext();
    void searchPrevious();
    // Sent up when the user has moved within the view
    void activity();
    // Sent up when the user want to exit this view
    // (switch to the next one)
    void exitView();

    void changeSearchLimits( LineNumber startLine, LineNumber endLine );
    void clearSearchLimits();

    void saveDefaultSplitterSizes();
    void sendSelectionToScratchpad();
    void replaceScratchpadWithSelection();
    void changeFontSize( bool increase );

    void addColorLabel( size_t label );
    void addNextColorLabel();
    void clearColorLabels();
    void highlightersChange();

public Q_SLOTS:
    // Makes the widget select and display the passed line.
    // Scrolling as necessary
    void trySelectLine( LineNumber newLine );
    void selectAndDisplayLine( LineNumber line );
    void selectPortionAndDisplayLine( LineNumber line, LinesCount nLines, LineColumn startCol,
                                      LineLength nSymbols );

    // Use the current QFP to go and select the next match.
    void searchForward() override;
    // Use the current QFP to go and select the previous match.
    void searchBackward() override;

    // Use the current QFP to go and select the next match (incremental)
    void incrementallySearchForward() override;
    // Use the current QFP to go and select the previous match (incremental)
    void incrementallySearchBackward() override;
    // Stop the current incremental search (typically when user press return)
    void incrementalSearchStop() override;
    // Abort the current incremental search (typically when user press esc)
    void incrementalSearchAbort() override;

    // Signals the follow mode has been enabled.
    void followSet( bool checked );

    // Signals the text wrap mode has been enabled.
    void textWrapSet( bool checked );

    // Signal the on/off status of the overview has been changed.
    void refreshOverview();

    // Make the view jump to the specified line, regardless of it
    // being on the screen or not. (does NOT Q_EMIT followDisabled() )
    void jumpToLine( LineNumber line );

    // Configure the setting of whether to show line number margin
    void setLineNumbersVisible( bool lineNumbersVisible );

    // Force the next refresh to fully redraw the view by invalidating the cache.
    // To be used if the data might have changed.
    void forceRefresh();

    void setSearchLimits( LineNumber startLine, LineNumber endLine );

private Q_SLOTS:
    void handlePatternUpdated();
    void addToSearch();
    void replaceSearch();
    void excludeFromSearch();
    void findNextSelected();
    void findPreviousSelected();
    void copy();
    void copyWithLineNumbers();
    void markSelected();
    void saveToFile();
    void saveSelectedToFile();
    void setSearchStart();
    void setSearchEnd();
    void setSelectionStart();
    void setSelectionEnd();
    void setQuickFindResult( bool hasMatch, const Portion& selection );
    void setColorLabel( QAction* action );
    void markAsEvent( EventDialogMode mode );

private:
    // Graphic parameters
    static constexpr int OverviewWidth = 27;
    static constexpr int HookThreshold = 300;
    static constexpr int PullToFollowHookedHeight = 10;

    // Width of the bullet zone, including decoration
    int bulletZoneWidthPx_;

    // Total size of all margins and decorations in pixels
    int leftMarginPx_ = 0;

    // Digits buffer (for numeric keyboard entry)
    DigitsBuffer digitsBuffer_;

    // Follow mode
    bool followMode_ = false;

    // ElasticHook for follow mode
    ElasticHook followElasticHook_;

    // Whether to show line numbers or not
    bool lineNumbersVisible_ = false;

    // Per-tab event tooltip lookup bound by the owning CrawlerWidget
    std::function<QString( LineNumber )> eventTooltipProvider_;

    // Pointer to the Overview object
    Overview* overview_ = nullptr;

    // Pointer to the OverviewWidget, this class doesn't own it,
    // but is responsible for displaying it (for purely aesthetic
    // reasons).
    OverviewWidget* overviewWidget_ = nullptr;

    bool selectionStarted_ = false;
    // Start of the selection (characters)
    FilePosition selectionStartPos_;
    // Current end of the selection (characters)
    FilePosition selectionCurrentEndPos_;
    QBasicTimer autoScrollTimer_;

    // Hovering state
    // Last line that has been hoovered on, -1 if none
    OptionalLineNumber lastHoveredLine_;
    OptionalLineNumber lastContentHoveredLine_;

    // Marks (left margin click)
    bool markingClickInitiated_ = false;
    OptionalLineNumber markingClickLine_;

    Selection selection_;
    RegularExpressionPattern searchPattern_;

    std::vector<QuickHighlighters> quickHighlighters_ = std::vector<QuickHighlighters>{ 10 };

    // Position of the view, those are crucial to control drawing
    // firstLine gives the position of the view,
    // lastLineAligned == true make the bottom of the last line aligned
    // rather than the top of the top one.
    LineNumber firstLine_;
    bool lastLineAligned_ = false;
    bool useTextWrap_ = false;
    LineColumn firstCol_ = 0_lcol;

    struct WrappedLineData {
        LineNumber lineNumber;
        size_t wrappedLineIndex;
        // Display text of the whole logical line, tabs expanded and ANSI codes
        // stripped: the mouse mapping measures pixel offsets in it.
        QString lineText;
        // The wrap part this visual row shows, in UTF-16 columns of lineText.
        // The parts tile the line, so the start of a part is also the number of
        // columns the parts before it cover.
        LineColumn partStart;
        LineLength partLength;
    };
    klogg::vector<WrappedLineData> wrappedLinesInfo_;

    LineNumber searchStart_;
    LineNumber searchEnd_;

    OptionalLineNumber selectionStart_;

    // Text handling
    int charWidth_ = 1;
    int charHeight_ = 10;

    // Popup menu
    QMenu* popupMenu_;
    QAction* copyAction_;
    QAction* copyWithLineNumbersAction_;
    QAction* copyWithColorAction_;
    QAction* markAction_;
    QAction* sendToScratchpadAction_;
    QAction* replaceInScratchpadAction_;
    QAction* saveToFileAction_;
    QAction* saveSelectedToFileAction_;
    QAction* findNextAction_;
    QAction* findPreviousAction_;
    QAction* addToSearchAction_;
    QAction* replaceSearchAction_;
    QAction* excludeFromSearchAction_;
    QAction* setSearchStartAction_;
    QAction* setSearchEndAction_;
    QAction* clearSearchLimitAction_;
    QAction* setSelectionStartAction_;
    QAction* setSelectionEndAction_;
    QAction* saveDefaultSplitterSizesAction_;
    QAction* markAsEventAction_;
    QAction* markAsBeginEventAction_;
    QAction* markAsEndEventAction_;
    HighlightersMenu* highlightersMenu_;
    QMenu* colorLabelsMenu_;

    std::map<QString, QShortcut*> shortcuts_;

    // Pointer to the CrawlerWidget's QFP object
    const QuickFindPattern* const quickFindPattern_;
    // Our own QuickFind object
    QuickFind* quickFind_;

#ifdef GLOGG_PERF_MEASURE_FPS
    // Performance measurement
    PerfCounter perfCounter_;
#endif

    // Vertical offset (in pixels) at which the first line of text is written
    int drawingTopOffset_ = 0;

    // Cache pixmap and associated info
    struct TextAreaCache {
        QPixmap pixmap_;
        bool invalid_;
        LineNumber first_line_;
        LineNumber last_line_;
        LineColumn first_column_;
        int actual_height_;
    };
    struct PullToFollowCache {
        QPixmap pixmap_;
        LineLength nb_columns_;
    };
    TextAreaCache textAreaCache_ = { {}, true, 0_lnum, 0_lnum, 0_lcol, 0 };
    PullToFollowCache pullToFollowCache_ = { {}, 0_length };

    // Frame-scoped gutter flags cache, see prefetchGutterFlags (B5c)
    bool gutterFlagsCacheValid_ = false;
    LineNumber gutterFlagsCacheFirstLine_ = 0_lnum;
    klogg::vector<GutterLineFlags> gutterFlagsCache_;
    QFontMetrics pixmapFontMetrics_;

    LinesCount getNbVisibleLines() const;
    LinesCount getNbBottomWrappedVisibleLines() const;
    LineLength getNbVisibleCols() const;
    bool shouldBottomAlignFrame() const;

    FilePosition convertCoordToFilePos( const QPoint& pos ) const;
    OptionalLineNumber convertCoordToLine( int yPos ) const;
    LineColumn convertCoordToColumn( int xPos ) const;

    void displayLine( LineNumber line );
    void moveSelection( LinesCount delta, bool isDeltaNegative );
    void moveSelectionUp();
    void moveSelectionDown();
    void jumpToStartOfLine();
    void jumpToEndOfLine();
    void jumpToRightOfScreen();
    void jumpToTop();
    void jumpToBottom();
    void selectWordAtPosition( const FilePosition& pos );

    void updateSearchLimits();

    void createMenu();

    void considerMouseHovering( int xPos, int yPos );

    LineLength maxLineLength( const klogg::vector<LineNumber>& lines ) const;

    // Save specified lines in range [begin, end) to a file
    void saveLinesToFile( LineNumber begin, LineNumber end );

    // Search functions (for n/N)
    using QuickFindSearchFn = void ( QuickFind::* )( Selection, QuickFindMatcher );
    void searchUsingFunction( QuickFindSearchFn searchFunction );

    void updateScrollBars();

    // The scroll bar addresses lines through an int; this is true when its
    // range was clamped to that limit (see updateScrollBars), that is when the
    // lines do not fit it one to one and the positions are compressed. The
    // mapping below only leaves the widget then.
    bool scrollRangeCompressed() const;

    LineNumber verticalScrollToLineNumber( int scrollPosition ) const;
    int lineNumberToVerticalScroll( LineNumber line ) const;
    double verticalScrollMultiplicator() const;

    void drawTextArea( QPaintDevice* paintDevice );

    // Qt boundary for klogg::render::composeLineHighlights: the UI turns
    // palette colours into Rgba, runs the parts that still live in the shell
    // (QuickFind matches), and converts the result back to QColor.
    struct LineHighlightResult {
        HighlightedMatchRanges matches;
        HighlighterMatchType matchType{ HighlighterMatchType::NoMatch };
        QColor lineMatchForeColor;
        QColor lineMatchBackColor;
    };
    LineHighlightResult computeLineHighlightMatches(
        const QString& expandedLine, const klogg::vector<AnsiSegment>& ansiSegments,
        LineNumber lineNumber, LineNumber searchStartIndex, LineNumber searchEndIndex,
        const Highlighter* patternHighlight,
        const klogg::vector<Highlighter>& additionalHighlighters, const QColor& quickFindBackColor,
        const HighlightedMatch* selectionPortion ) const;

    QPixmap drawPullToFollowBar( int width, qreal pixelRatio );

    void disableFollow();

    // Utils functions
    void updateGlobalSelection();

    void selectAndDisplayRange( FilePosition pos );
};

#endif
