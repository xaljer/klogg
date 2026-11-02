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

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "klogg/api_engine.h"
#include "klogg/api_session.h"
#include "klogg/api_types.h"

#include "eventvisualization.h"
#include "executor.h"
#include "highlighterset.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "synchronization.h"

namespace klogg::api {

// C4-5 read channel: read-only Session methods run on the calling thread by
// default; setting KLOGG_READ_CHANNEL=queue restores the pre-C4-5 behaviour of
// marshalling every call onto the session worker. Registered for removal
// after one validation cycle (see execution-plan.md C4-5).
bool readChannelQueued();

// Owns all engine objects (LogData/LogFilteredData). Write/control methods
// must be called on the executor thread (the *OnWorker ones); the read
// methods are safe to call from any thread.
class EngineContext {
public:
    EngineContext();

    struct SearchEntry {
        // Session-owned; cancelSearch drops the reference so a cancelled
        // search is freed right away. The custom deleter posts the deletion
        // back to the executor and keeps the owning LogData alive until that
        // deletion ran (engine hard constraint: filtered data dies first).
        std::shared_ptr<LogFilteredData> filteredData;
        std::vector<ProgressCallback> callbacks;
        std::chrono::steady_clock::time_point startTime;
        std::optional<uint32_t> maxResults;
        LinesCount startTotalLines = 0_lcount;
        SearchParams params;
        LineNumber scannedToLine = 0_lnum;

        mutable std::mutex stateMutex;
        LinesCount totalMatches = 0_lcount;
        int progressPercent = 0;
        bool completed = false;
        bool cancelled = false;
    };

    struct ExtractionEntry {
        // Dedicated LogFilteredData, not shared with any search: the events
        // search writes into its Events array
        std::shared_ptr<LogFilteredData> filteredData;
        // Config and rules snapshotted at start so later CRUD cannot mutate
        // a running extraction
        ::LogTypeConfig logType;
        std::vector<::EventRule> rules;
        // Populated when the events search completes
        std::vector<::LogEvent> events;
        std::vector<::SliceLineRange> ranges;
        std::chrono::steady_clock::time_point startTime;
        // Scan range actually searched (endLine exclusive)
        LineNumber startLine = 0_lnum;
        LineNumber endLine = 0_lnum;

        mutable std::mutex stateMutex;
        LinesCount processedLines = 0_lcount;
        uint32_t eventsFound = 0;
        int progressPercent = 0;
        bool completed = false;
        bool cancelled = false;
    };

    struct FileEntry {
        // Session-owned; destroyed when the reference count reaches zero. The
        // custom deleter posts the deletion back to the executor, so the last
        // reference may drop on any thread.
        std::shared_ptr<LogData> logData;

        // Guards info / marks / eventLines (+ eventLinesSource): written on
        // the executor, read by the read channel on arbitrary threads. No
        // callback or event emission may happen while holding it.
        mutable SharedMutex fieldsMutex_;
        FileInfo info;
        // Session-level per-file marks, independent of LogFilteredData marks
        std::set<LineNumber> marks;
        // Matches the engine default: LogData adds the file to the global
        // FileWatcher after every successful indexing
        bool watchEnabled = true;
        // Same-path opens share one entry: each openFile* bumps this and
        // closeFile drops it; the entry is destroyed only at zero
        uint32_t refCount = 1;
        // Dedup key derived from the path (canonical, case-folded on Windows)
        std::string normalizedPath;
        // True from attach until the first successful indexing completion
        bool indexingInProgress = false;
        // Forced encoding requested at open, applied on the first completion
        std::optional<std::string> requestedEncoding;
        bool encodingApplied = false;
        // Openers waiting for the in-flight indexing to complete
        std::vector<std::shared_ptr<FileCallback>> pendingOpenCallbacks;
        // Async reload/reindex completions to report (reloadFileAsync and
        // the reindexing form of setEncodingAsync)
        std::vector<std::shared_ptr<FileCallback>> pendingReindexCallbacks;

        // Guards the searches and extractions maps: entries are only ever
        // inserted (on the executor, unique lock), never individually erased
        // (the whole FileEntry dies together), so a found node stays alive
        // while the FileEntry shared_ptr is held.
        mutable SharedMutex searchesMutex_;
        std::map<SearchId, SearchEntry> searches;
        std::map<ExtractionId, ExtractionEntry> extractions;

        // Event lines of the latest completed extraction, backing the
        // LineFlagEvent gutter bit; dropped when that extraction is cancelled
        // or the file is reindexed
        std::set<LineNumber> eventLines;
        std::optional<ExtractionId> eventLinesSource;
        // Categories selected for the projections (empty = all); applied when
        // event lines are mirrored into the searches' filtered data
        std::set<std::string> eventCategoryFilter;
    };

    // Guards the entries map itself: insert/erase only on the executor
    // (unique lock); readers take a shared lock, copy the shared_ptr out and
    // release. Entry *contents* are executor-serialized or guarded by the
    // entry's own mutexes.
    mutable SharedMutex entriesMutex_;
    std::map<FileId, std::shared_ptr<FileEntry>> entries;

    // In-memory highlight model owned by the session: the flat, effective rule
    // list the render path consumes plus the colour label palette. The named
    // sets, their active flags and all persistence live in the shell (UI),
    // which merges and pushes the effective rules through the API.
    HighlighterSet activeHighlighters;
    klogg::vector<QuickHighlighter> quickHighlighters;
    // Session-owned (not the getInstance() singleton) so sessions stay isolated.
    // In-memory only: the shell loads log_visualization.json and pushes the
    // configs through replaceLogTypeConfigs; the core never touches the file.
    EventVisualizationManager eventVisualizationManager;

    // One open file's restorable state, supplied/collected by the session
    struct FileState {
        std::string path;
        std::string encoding;
        std::vector<uint64_t> marks;
    };

    // Compilable snapshot of the highlight state the paint path needs: the
    // merged active sets and the colour label palette. Everything in here is
    // built on the worker and published to the read channel, so the colours a
    // frame is composed with never come from a lazy compile racing the paint.
    struct PaintHighlightState {
        HighlighterSet activeSet;
        klogg::vector<QuickHighlighter> quickHighlighters;
    };

    // Highlight state published for the paint path: rebuilt on the worker by
    // publishPaintHighlights() after every highlight mutation and read
    // atomically from any thread, so a frame never touches the collection and
    // never triggers a lazy compile. The free atomic accessors are used because
    // std::atomic<std::shared_ptr<...>> needs a newer standard library than the
    // toolchains this project still builds with. Mutable only because those
    // accessors want a non-const pointer.
    mutable std::shared_ptr<const PaintHighlightState> paintHighlights;

    // Shared ownership: the deleter-post scheme (see session_impl.cpp) hands a
    // reference to every engine object so a last reference dropped on any
    // thread always posts to a live (possibly stopped) executor
    std::shared_ptr<SessionExecutor> executor;
    std::function<void( const std::string&, const std::string& )> eventSink;

    // Engine behavior parameters injected at Session::create(); mutable so
    // Session::updateOptions can change them at runtime.
    std::shared_ptr<EngineOptionsHolder> optionsHolder;

    Result<FileInfo> openFileOnWorker( FileId id, const std::string& path,
                                       const std::optional<std::string>& encoding,
                                       bool allowMissing );
    Result<FileInfo> openFileAsyncOnWorker( FileId id, const std::string& path,
                                            const std::optional<std::string>& encoding,
                                            FileCallback onDone, bool allowMissing );
    Result<void> closeFileOnWorker( FileId id );
    Result<FileInfo> reloadFileOnWorker( FileId id );
    Result<void> reloadFileAsyncOnWorker( FileId id, FileCallback onDone );
    Result<void> interruptLoadingOnWorker( FileId id );
    Result<void> watchFileOnWorker( FileId id, bool enabled );
    Result<void> checkFileChangesOnWorker( FileId id );
    Result<void> extendSearchOnWorker( SearchId id, LineNumber toLine );
    Result<void> setEncodingOnWorker( FileId id, const std::string& encoding );
    Result<void> setEncodingAsyncOnWorker( FileId id, const std::string& encoding,
                                           FileCallback onDone );
    Result<SearchId> runSearchOnWorker( FileId id, SearchId searchId, const SearchParams& params );
    Result<LinesCount> setSearchVisibilityOnWorker( SearchId id, bool matches, bool marks,
                                                    bool events );
    Result<void> cancelSearchOnWorker( SearchId id );
    Result<void> interruptSearchOnWorker( SearchId id );
    void addProgressCallbackOnWorker( SearchId id, ProgressCallback cb );
    Result<ExtractionId> extractEventsOnWorker( FileId fileId, ExtractionId extractionId,
                                                const ExtractionParams& params );
    Result<ExtractionProgress> extractionProgressOnWorker( ExtractionId id );
    Result<std::vector<ExtractedEvent>> extractedEventsOnWorker( ExtractionId id, uint64_t from,
                                                                 uint32_t count );
    Result<void> cancelExtractionOnWorker( ExtractionId id );
    Result<std::vector<SliceRange>> sliceRangesOnWorker( ExtractionId id );
    Result<void> setEventCategoryFilterOnWorker( FileId id, std::vector<std::string> categories );
    Result<EventExportResult>
    exportEventsOnWorker( ExtractionId id, const std::string& outputPath, ExportFormat format,
                          const std::optional<std::vector<std::string>>& categories );
    // Read channel (C4-5): safe to call from any thread; snapshot semantics =
    // the state committed when the read starts, in-flight indexing is visible
    // at block commit granularity, no stronger promise
    Result<FileInfo> fileInfo( FileId id ) const;
    Result<std::vector<FileInfo>> openFiles() const;
    Result<std::vector<LineData>> getLines( FileId id, LineNumber from, LinesCount count,
                                            AnsiMode ansi ) const;
    Result<std::vector<LineData>> getLinesBatch( FileId id, const std::vector<LineNumber>& lines,
                                                 AnsiMode ansi ) const;
    Result<SearchResult> searchProgress( SearchId id ) const;
    Result<std::vector<LineNumber>> searchMatches( SearchId id, uint64_t from,
                                                   uint32_t count ) const;
    Result<std::optional<uint64_t>> searchMatchIndex( SearchId id, LineNumber line ) const;
    Result<std::vector<LineData>> getFilteredLines( SearchId id, uint64_t fromIndex, uint32_t count,
                                                    AnsiMode ansi ) const;
    Result<std::vector<LineNumber>> filteredLineNumbers( SearchId id, uint64_t fromIndex,
                                                         uint32_t count ) const;
    Result<LinesCount> getFilteredLineCount( SearchId id ) const;
    Result<std::optional<uint64_t>> filteredLineIndex( SearchId id, LineNumber line ) const;
    Result<std::optional<uint64_t>> filteredLineIndexAtOrBefore( SearchId id,
                                                                 LineNumber line ) const;
    Result<LineLength> filteredMaxLineLength( SearchId id ) const;
    Result<std::vector<uint8_t>> lineFlags( FileId id, std::optional<SearchId> searchId,
                                            LineNumber from, LinesCount count ) const;
    Result<std::vector<uint8_t>> lineFlagsBatch( FileId id, std::optional<SearchId> searchId,
                                                 const std::vector<LineNumber>& lines ) const;
    Result<double> scrollMultiplier( FileId id ) const;
    Result<LineNumber> scrollPosToLine( FileId id, int64_t scrollPos ) const;
    Result<int64_t> lineToScrollPos( FileId id, LineNumber line ) const;
    Result<ViewportResult> renderViewport( FileId id, std::optional<SearchId> searchId,
                                           LineNumber firstLine, LinesCount visibleLines,
                                           const ViewportOptions& options ) const;
    Result<RenderedHtml> renderLinesHtml( FileId id, std::optional<SearchId> searchId,
                                          const std::vector<LineNumber>& lines,
                                          const HtmlRenderOptions& options ) const;
    // Rebuilds and publishes the paint highlight snapshot; worker thread only
    // (or session construction, before the session is handed out)
    void publishPaintHighlights();
    Result<std::vector<LineNumber>> listMarks( FileId id ) const;
    Result<std::optional<LineNumber>> markAfter( FileId id, LineNumber line ) const;
    Result<std::optional<LineNumber>> markBefore( FileId id, LineNumber line ) const;
    Result<void> addMarksOnWorker( FileId id, const std::vector<LineNumber>& lines );
    Result<void> removeMarksOnWorker( FileId id, const std::vector<LineNumber>& lines );
    Result<std::string> saveSessionOnWorker() const;
    void destroyAllOnWorker();

    // ====== In-memory highlight state (no persistence; the shell pushes it) ======
    Result<void> setHighlighters( const std::vector<HighlightRule>& rules );
    Result<void> setQuickHighlighters( const std::vector<QuickHighlighterDef>& defs );

    // ---- Event visualization log types (in-memory only) ----
    Result<std::vector<LogTypeConfig>> listLogTypeConfigs() const;
    Result<void> replaceLogTypeConfigs( const std::vector<LogTypeConfig>& configs );
    // Copy of the first config with this name; the extraction pipeline
    // snapshots it so later replacement cannot mutate a running extraction
    std::optional<::LogTypeConfig> findLogTypeConfigByName( const std::string& name ) const;
    // Engine-side access to the parse/extract/convert helpers
    EventVisualizationManager& eventVisualization();

    // Compilable snapshot of the highlight state the paint path needs
    PaintHighlightState paintHighlightState() const;

    std::string saveSession( const std::vector<FileState>& files ) const;
    // Replaces the shared state collections and returns the files to reopen
    Result<std::vector<FileState>> restoreSession( const std::string& json );

private:
    // Creates the entry, attaches the file and starts indexing without
    // waiting for completion; onOpenIndexingFinished does the finalization
    Result<FileInfo> beginOpenFileOnWorker( FileId id, const std::string& path,
                                            const std::optional<std::string>& encoding,
                                            std::string normalizedPath, bool allowMissing );
    // Blocks (nested event loop) until the in-flight indexing of the entry
    // completes; used by the synchronous open paths
    Result<FileInfo> waitForOpenCompletionOnWorker( FileId id );
    void onOpenIndexingFinished( FileId id, LoadingStatus status );
    // Lookup helpers usable from any thread: the returned shared_ptr keeps
    // the entry alive after the lock is released
    std::shared_ptr<FileEntry> findFile( FileId id ) const;
    std::shared_ptr<FileEntry> findFileByNormalizedPath( const std::string& normalizedPath ) const;
    // Resolves a search to its owning file (kept alive) plus the stable
    // SearchEntry node (alive while the file entry is held)
    std::pair<std::shared_ptr<FileEntry>, SearchEntry*> findSearchEntry( SearchId id ) const;
    std::pair<std::shared_ptr<FileEntry>, ExtractionEntry*>
    findExtractionEntry( ExtractionId id ) const;
    // Copies the filtered data reference of a live search, or returns the
    // cancelled/not-found error; callers release stateMutex before reading
    Result<std::shared_ptr<LogFilteredData>> searchFilteredData( const SearchEntry& entry,
                                                                 SearchId id ) const;
    // Shared file/search resolution of the two lineFlags variants
    Result<std::pair<std::shared_ptr<FileEntry>, std::shared_ptr<LogFilteredData>>>
    lineFlagsSource( FileId id, std::optional<SearchId> searchId ) const;
    // Re-reads size/line counts from the engine after a reindex
    void refreshFileInfo( FileEntry& entry );
    // Re-indexes an entry whose initial indexing was interrupted before any
    // completion committed a display codec: an open without an encoding
    // override re-detects from scratch (reloadFile's committed-codec
    // preservation does not apply to a never-indexed entry)
    void reindexInterruptedEntry( FileEntry& fileEntry );
    // LogData re-adds itself to the FileWatcher on every successful indexing,
    // so the preference has to be re-applied after each reindex
    void applyWatchPreference( const FileEntry& entry );
    // Mirrors the file's latest completed extraction (filtered by category)
    // into the events array of every live search projection of the file;
    // called when the extraction, the filter, a search's visibility or the
    // file's search set change
    void syncEventLinesToSearches( FileEntry& fileEntry );
    // Mirrors a mark mutation into the searches of the file that project
    // marks, so a mark added after the search shows up without a visibility
    // flip; projections without Marks are left alone
    void syncMarkLinesToSearches( FileEntry& fileEntry, const std::vector<LineNumber>& lines,
                                  bool added );
    void onSearchProgressed( FileId fileId, SearchId searchId, LinesCount nbMatches, int progress );
    void onExtractionProgressed( FileId fileId, ExtractionId extractionId, LinesCount nbEvents,
                                 int progress );
    void emitEvent( const std::string& eventType, const std::string& json );
};

struct Session::Impl {
    // Shared with every engine object deleter (C4-5): outlives Impl so a
    // last reference dropped after ~Impl posts to a stopped-but-live executor
    // instead of dereferencing freed memory
    std::shared_ptr<SessionExecutor> worker;
    // Owned manually: it must be deleted only after the worker thread stopped
    EngineContext* context = nullptr;

    // Allocated on caller threads (openFileAsync returns before the worker runs)
    std::atomic<FileId> nextFileId{ 1 };
    std::atomic<SearchId> nextSearchId{ 1 };
    std::atomic<ExtractionId> nextExtractionId{ 1 };

    mutable std::mutex handlersMutex;
    std::vector<EventHandler> handlers;

    explicit Impl( EngineOptions options );
    ~Impl();

    Impl( const Impl& ) = delete;
    Impl& operator=( const Impl& ) = delete;

    // Serializes a call onto the worker thread; runs inline when the caller
    // is already on it (prevents self-deadlock from callbacks)
    template <typename Fn>
    auto dispatch( Fn&& fn ) const -> std::invoke_result_t<Fn, EngineContext*>
    {
        using R = std::invoke_result_t<Fn, EngineContext*>;
        return worker->dispatch( [ this, &fn ]() -> R { return fn( context ); } );
    }

    void emitEvent( const std::string& eventType, const std::string& json ) const;
};

} // namespace klogg::api
