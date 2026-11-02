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

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "klogg/api_engine.h"
#include "klogg/api_events.h"
#include "klogg/api_render.h"
#include "klogg/api_state.h"

// Engine behavior parameters (core type), injected by the shell/app
#include "engine_options.h"

namespace klogg::api {

// Main entry point of the Core API. Consumers: CLI, MCP, Web Server, Qt UI.
//
// Thread-safety contract: all methods may be called from any thread.
// Write/control methods serialize internally on a per-session worker; the
// read-only methods (line fetching, search projection queries, marks/flags
// queries) run directly on the calling thread with snapshot semantics: a read
// observes the state committed when it starts, in-flight indexing/search is
// visible at block commit granularity, no stronger promise (C4-5; the
// KLOGG_READ_CHANNEL=queue environment variable restores full marshalling).
// Callbacks and event handlers fire on the session worker thread;
// consumers marshal as needed.
//
// A Session is one complete working context: it owns the worker thread, open
// files (mmap + indexes), searches, and all shared state (highlights, filters,
// marks, history). FileId/SearchId are scoped to their owning Session.
// Multiple instances may coexist (mainly for tests), but real deployments use
// a single Session per process: desktop UI shares it across windows, a server
// shares it across clients. The same file opened in two Sessions is indexed
// twice; there is no cross-session dedup or state sharing.
//
// The full surface is declared up front and implemented in vertical slices;
// unimplemented methods return ErrorCode::InternalError until their slice lands.
class Session {
public:
    // Assumes the platform services of the process are already initialized:
    // the caller (composition root) owns its application object and the
    // persistent stores the engine reads shared state from. Creating a Session
    // neither builds an application object nor initializes storage.
    static Result<std::unique_ptr<Session>> create( const EngineOptions& options = {} );
    ~Session();

    Session( const Session& ) = delete;
    Session& operator=( const Session& ) = delete;

    // Applies new engine behavior parameters at runtime. They take effect for
    // operations started afterwards (opens, searches); in-flight operations
    // keep the snapshot they started with.
    Result<void> updateOptions( const EngineOptions& options );

    // ====== Engine API (performance-critical path) ======

    // ---- File operations ----
    // Supports: local files / archives (.zip .tar .gz) / remote URLs (http https)
    //
    // Same-path opens (compared by canonical path) share a single entry: they
    // return the same FileId and bump an internal reference count instead of
    // attaching and indexing twice. An encoding override only applies to the
    // first open of a path. With allowMissing a non-existent path opens as an
    // empty file (the Qt UI relies on this to follow not-yet-created logs);
    // otherwise it fails with FileNotFound.
    Result<FileInfo> openFileSync( const std::string& path,
                                   const std::optional<std::string>& encoding = {},
                                   bool allowMissing = false );
    // The entry is created synchronously (file attached, indexing started) so
    // the returned FileId is usable right away; onDone still fires on the
    // worker thread when indexing completes. A synchronous error (missing
    // file, unknown encoding) is reported through the return value only.
    // A same-path open while indexing is in flight returns the pending
    // FileInfo of the existing entry and fires onDone when that completes.
    Result<FileInfo> openFileAsync( const std::string& path, FileCallback onDone,
                                    const std::optional<std::string>& encoding = {},
                                    bool allowMissing = false );
    // Drops one reference of the file; the engine objects are destroyed only
    // when the count reaches zero. Unknown id -> InvalidFileId.
    Result<void> closeFile( FileId id );
    Result<FileInfo> reloadFile( FileId id ); // force full reindex
    // Asynchronous twin of reloadFile: returns once the reindex has been
    // triggered; onDone (when given) fires on the worker thread with the
    // refreshed FileInfo once the reindex completes. An interrupt racing the
    // trigger is skipped so the callback tracks the triggered reindex; if
    // that reindex is itself interrupted, the callback fires on the next
    // completed reindex instead
    Result<void> reloadFileAsync( FileId id, FileCallback onDone );
    // Aborts an in-flight load/index; the entry stays open (the UI Stop
    // action maps to this, LogData::interruptLoading underneath)
    Result<void> interruptLoading( FileId id );

    Result<FileInfo> fileInfo( FileId id ) const;
    Result<std::vector<FileInfo>> openFiles() const;

    Result<void> watchFile( FileId id, bool enabled );

    // Cheap change probe: stats the file and lets the engine enqueue an
    // incremental (append-only) reindex when needed. Returns without waiting;
    // a completed reindex surfaces as "file.reindexed".
    Result<void> checkFileChanges( FileId id );

    // Extend a completed search to a newer end line, scanning only the new
    // range. The caller must use the same pattern/flags as the original
    // search; false/unsupported cases are reported as errors and the caller
    // falls back to a fresh runSearch.
    Result<void> extendSearch( SearchId id, LineNumber toLine );

    Result<void> setEncoding( FileId id, const std::string& encoding );
    // Asynchronous twin of setEncoding: returns once the codec is applied
    // (and any reindex the mib change forces has been triggered); onDone
    // fires on the worker thread after that reindex completes, or right away
    // when the codec change needs no reindex
    Result<void> setEncodingAsync( FileId id, const std::string& encoding, FileCallback onDone );
    Result<std::vector<std::string>> supportedEncodings() const;

    // ---- Line data ----
    Result<std::vector<LineData>> getLines( FileId id, LineNumber from, LinesCount count,
                                            AnsiMode ansi = AnsiMode::Raw );
    // Batch fetch of arbitrary file lines in one engine round trip (the
    // engine reads the whole set with a single file session); one entry per
    // requested line, in request order, out-of-file lines yield the engine's
    // padding line like getLines past the end
    Result<std::vector<LineData>> getLinesBatch( FileId id, const std::vector<LineNumber>& lines,
                                                 AnsiMode ansi = AnsiMode::Raw );

    // ---- Scroll position mapping (for consumers with an int scroll range) ----
    // A view that addresses lines through an integer scroll range (a scroll
    // bar) converts between line numbers and positions with these; a file
    // deeper than the range can address is addressed through compressed
    // positions scaled by the compression factor (scrollMultiplierForLines).
    // Both directions are a plain linear scale with no clamping - a consumer
    // clamps to its own range - so lineToScrollPos is defined past the end of
    // the file and scrollPosToLine beyond its last line as well; a negative
    // position reads as 0.
    //
    // Read-only: same inlined read channel as getLines. The factor follows the
    // file's line count, so it grows with an in-flight index.
    Result<double> scrollMultiplier( FileId id ) const;
    Result<LineNumber> scrollPosToLine( FileId id, int64_t scrollPos ) const;
    Result<int64_t> lineToScrollPos( FileId id, LineNumber line ) const;

    // ---- Viewport composition ----
    // One call per painted frame: fetches the visible lines, their gutter
    // flags and the current highlight state, then composes the whole frame
    // (tab expansion, ANSI, highlights, wrapping and chunk layout) so a
    // consumer only has to paint the result.
    //
    // `firstLine` is a view line of the file (the same numbering getLines
    // uses); `searchId`, when given, supplies the match bit of the gutter
    // flags. Read-only: it runs on the calling thread with the same snapshot
    // semantics as getLines, and observes the highlight state as of the
    // moment it is called.
    Result<ViewportResult> renderViewport( FileId id, std::optional<SearchId> searchId,
                                           LineNumber firstLine, LinesCount visibleLines,
                                           const ViewportOptions& options );

    // Copy-with-colour twin of renderViewport: composes the given source lines
    // (arbitrary order, repeats allowed) exactly like the paint path does and
    // serializes them as the HTML fragment and plain text of the Qt copy
    // action. `searchId`, when given, is accepted for parity with
    // renderViewport; the copy carries no gutter. Read-only, same read channel
    // and snapshot semantics as renderViewport.
    Result<RenderedHtml> renderLinesHtml( FileId id, std::optional<SearchId> searchId,
                                          const std::vector<LineNumber>& lines,
                                          const HtmlRenderOptions& options );

    // ---- Search ----
    Result<PatternInfo> validatePattern( const std::string& pattern, bool isRegex = true ) const;

    // Each call starts a new search and returns a fresh SearchId.
    Result<SearchId> runSearch( FileId id, const SearchParams& params );

    // Boolean search, ExprTk syntax: "error" and ("timeout" or "panic")
    Result<SearchId> runBooleanSearch( FileId id, const std::string& expression );

    Result<SearchResult> searchProgress( SearchId id ) const;
    Result<std::vector<LineNumber>> searchMatches( SearchId id, uint64_t from, uint32_t count );
    // Index of the first match at or after `line`; nullopt when the search has
    // no matches or every match is before `line`
    Result<std::optional<uint64_t>> searchMatchIndex( SearchId id, LineNumber line ) const;
    // Lines of a search's filtered view: fromIndex/count address the lines
    // visible under the current visibility (matches only by default),
    // LineData.num carries the original file line number
    Result<std::vector<LineData>> getFilteredLines( SearchId id, uint64_t fromIndex, uint32_t count,
                                                    AnsiMode ansi = AnsiMode::Raw );
    // Text-less twin of getFilteredLines: same addressing, only the original
    // file line numbers come back (for consumers that never render the text,
    // e.g. the overview sidebar's union projection pull)
    Result<std::vector<LineNumber>> filteredLineNumbers( SearchId id, uint64_t fromIndex,
                                                         uint32_t count );
    // Selects which line classes a search's filtered view exposes and returns
    // the union line count under the new visibility. Enabling marks mirrors
    // the file's session-level marks into the engine view
    Result<LinesCount> setSearchVisibility( SearchId id, bool matches, bool marks, bool events );
    // Line count of a search's filtered view under its current visibility
    Result<LinesCount> getFilteredLineCount( SearchId id ) const;
    // Index of a source line in the search's filtered projection (the union
    // of the classes enabled by setSearchVisibility); nullopt when the line
    // is not in the projection. Unlike searchMatchIndex this covers marks
    // and events, not just matches
    Result<std::optional<uint64_t>> filteredLineIndex( SearchId id, LineNumber line ) const;
    // Floor variant: rank of the last projected member at or before `line`;
    // nullopt when the projection is empty or `line` precedes every member
    // (the native getLineIndexNumber clamps that case to 0, so its callers
    // land on the first projected line)
    Result<std::optional<uint64_t>> filteredLineIndexAtOrBefore( SearchId id,
                                                                 LineNumber line ) const;
    // Max line length within the search's filtered set (matches, marks and
    // events, visibility-independent), matching LogFilteredData::getMaxLength;
    // backs the filtered view's horizontal scroll range, so it must not be
    // approximated with the full-file max
    Result<LineLength> filteredMaxLineLength( SearchId id ) const;
    Result<void> cancelSearch( SearchId id );
    // Stops an in-flight search but keeps the id valid and the matches found
    // so far readable (the UI Stop action and tab switching map to this;
    // cancelSearch additionally invalidates the id and drops the results)
    Result<void> interruptSearch( SearchId id );
    void onSearchProgress( SearchId id, ProgressCallback cb );

    // ---- Mark navigation (over the per-file marks of the state section) ----
    // First mark strictly after `line` / last mark strictly before `line`;
    // nullopt when there is none
    Result<std::optional<LineNumber>> markAfter( FileId id, LineNumber line ) const;
    Result<std::optional<LineNumber>> markBefore( FileId id, LineNumber line ) const;

    // ---- Line flags (gutter markers for both views) ----
    // One flag byte per source file line over [from, from + count), clamped
    // to the file size; bit values are the LineFlag constants. The match bit
    // requires a searchId belonging to this file; the event bit mirrors the
    // file's latest completed extraction
    Result<std::vector<uint8_t>> lineFlags( FileId id, std::optional<SearchId> searchId,
                                            LineNumber from, LinesCount count ) const;
    // Same flag bytes as lineFlags but for an explicit set of source lines
    // (e.g. the non-contiguous lines backing a filtered view frame); one
    // byte per requested line, in request order, 0 for out-of-file lines
    Result<std::vector<uint8_t>> lineFlagsBatch( FileId id, std::optional<SearchId> searchId,
                                                 const std::vector<LineNumber>& lines ) const;

    // ---- Event subscription ----
    void subscribe( EventHandler handler );
    // Events:
    //   "file.indexed"      { fileId, totalLines, encoding }
    //   "file.changed"      { fileId, reason: "modified"|"truncated" }
    //   "file.reindexed"    { fileId, totalLines }
    //   "file.indexing_failed" { fileId, reason: "interrupted"|"no memory" }
    //     Fires for a non-successful indexing completion of an open file:
    //     a deliberate interruptLoading (initial load or reindex) or a
    //     terminal reindex failure. A failed initial open fires it too, just
    //     before the entry is dropped; that path also reports through
    //     openFileAsync's onDone.
    //   "indexing.progress" { fileId, progress: 0-100 }
    //   "download.progress" { fileId, bytesReceived, totalBytes, progress }
    //   "search.progress"   { searchId, matched, total, ratio: 0-1 }
    //   "search.completed"  { searchId, totalMatches, durationMs }
    //   "event.extraction.progress"  { fileId, extractionId, progress: 0-1, eventsFound }
    //   "event.extraction.completed" { fileId, extractionId, eventCount, durationMs }
    //   "marks.changed"     { fileId }
    //     Fires after the session-level marks of a file were mutated through
    //     addMarks/removeMarks (including the truncation clear and the
    //     restoreSession replay); consumers re-pull listMarks
    //   "engine.error"      { fileId, kind: "tooLongLines"|"exception", message }
    //     Engine failure on a file (indexing or search). The engine only
    //     reports it: telling the user is the shell's call, so the payload
    //     carries no presentation and kind is a stable string, not an enum

    // ---- System ----
    std::string version() const;

    // ====== Shared state API (consistent view for all consumers) ======
    // These collections are in-memory only: the Core API never reads or writes
    // the settings document for them. The shell owns persistence: it loads the
    // stores at startup and pushes the effective rules here, then saves after
    // every mutation.

    // ---- Highlight rules ----
    // The core knows nothing about named sets or which of them are active: the
    // shell merges its active sets and pushes the resulting flat, ordered rule
    // list. The whole set is replaced at once (an empty list clears it).
    Result<void> setHighlighters( const std::vector<HighlightRule>& rules );

    // ---- Quick highlighters (fixed-size color label palette) ----
    // The whole palette is replaced at once; the words assigned to a slot are
    // transient per-view state and stay with the consumer.
    Result<void> setQuickHighlighters( const std::vector<QuickHighlighterDef>& defs );

    // ---- Line marks ----
    Result<std::vector<LineNumber>> listMarks( FileId id ) const;
    Result<void> addMarks( FileId id, const std::vector<LineNumber>& lines );
    Result<void> removeMarks( FileId id, const std::vector<LineNumber>& lines );

    // ---- Session persistence ----
    // saveSession JSON: open files (with encoding and marks) and the effective
    //   highlight rules; UI-only user data (filters, saved searches, search
    //   history, recent files, shortcuts, event configs) is persisted by the
    //   shell itself
    // restoreSession replaces the shared state and reopens the files one by one,
    //   skipping (and reporting) files that fail to open
    Result<std::string> saveSession() const;
    Result<void> restoreSession( const std::string& json );

    // ====== Event visualization API (log -> structured events -> Perfetto) ======

    // ---- Log type configs (in-memory only; the shell owns persistence) ----
    // The whole list is replaced at once (an empty list clears it). The shell
    // loads log_visualization.json, pushes the configs here, and saves after
    // every mutation.
    Result<std::vector<LogTypeConfig>> listLogTypeConfigs() const;
    Result<void> replaceLogTypeConfigs( const std::vector<LogTypeConfig>& configs );

    // ---- Event extraction (engine: search pipeline + rule matching) ----
    // Handle-based async model, mirroring the search API family.
    Result<ExtractionId> extractEvents( FileId id, const ExtractionParams& params );
    Result<ExtractionProgress> extractionProgress( ExtractionId id ) const;
    Result<std::vector<ExtractedEvent>> extractedEvents( ExtractionId id, uint64_t from,
                                                         uint32_t count );
    Result<void> cancelExtraction( ExtractionId id );
    // Paired begin/end ranges of slice events (for the UI slice overlay)
    Result<std::vector<SliceRange>> sliceRanges( ExtractionId id ) const;
    // Restricts which categories of the file's latest completed extraction
    // join the searches' filtered projections (the events half of the union
    // when the Events visibility is on); empty = all categories. The gutter
    // event bit (lineFlags) is not filtered, matching the pre-API behavior.
    Result<void> setEventCategoryFilter( FileId id, std::vector<std::string> categories );

    // ---- Export & utilities ----
    // categories restricts the export to the given event categories; the
    // filter is applied at export time and the stored extraction is untouched.
    // nullopt = all categories; an empty vector matches nothing. All
    // EventExportResult statistics are computed over the filtered set.
    Result<EventExportResult> exportEvents( ExtractionId id, const std::string& outputPath,
                                            ExportFormat format = ExportFormat::Json,
                                            std::optional<std::vector<std::string>> categories
                                            = std::nullopt );
    // Generate a regex with named capture groups from selected text
    // (the "Mark as Event" flow); pure string logic, usable by any consumer
    Result<std::string> generateEventPattern( const std::string& selectedText ) const;

private:
    Session( EngineOptions options ); // created via the factory method
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace klogg::api
