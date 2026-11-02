/*
 * Copyright (C) 2009, 2010, 2013, 2014, 2015 Nicolas Bonnefon and other contributors
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

#ifndef LOGDATA_H
#define LOGDATA_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "abstractlogdata.h"
#include "encoding.h"
#include "engine_options.h"
#include "fileholder.h"
#include "filewatcher.h"
#include "loadingstatus.h"
#include "logdataoperation.h"
#include "logdataworker.h"
#include "posttarget.h"

class LogFilteredData;

// Thrown when trying to attach an already attached LogData
class CantReattachErr {};

// The consumer-facing notifications LogData forwards to its owner. All of
// them are delivered through the PostTarget (i.e. on the owner thread).
struct LogDataCallbacks {
    std::function<void( int )> onLoadingProgressed;
    std::function<void( LoadingStatus )> onLoadingFinished;
    std::function<void( MonitoredFileStatus )> onFileChanged;

    // Engine-level errors; the handler must marshal to the UI thread itself.
    EngineErrorHandler onError;
};

// Represents a complete set of data to be displayed (ie. a log file content)
// This class is thread-safe.
class LogData : public AbstractLogData {
public:
    // postTarget injects a task into the thread owning the LogData and must
    // never run it inline, the callbacks fire on that thread.
    LogData( PostTarget postTarget, LogDataCallbacks callbacks = {},
             std::shared_ptr<EngineOptionsHolder> optionsHolder = {} );
    ~LogData();

    LogData( const LogData& ) = delete;
    LogData& operator=( const LogData&& ) = delete;

    LogData( LogData&& ) = delete;
    LogData& operator=( LogData&& ) = delete;

    // Attaches the LogData to a file on disk
    // It starts the asynchronous indexing and returns (almost) immediately
    // Attaching to a non existant file works and the file is reported
    // to be empty.
    // Reattaching is forbidden and will throw.
    void attachFile( const std::string& fileName );
    // Interrupt the loading and report a null file.
    // Does nothing if no loading in progress.
    void interruptLoading();
    // Creates a new filtered data.
    // ownership is passed to the caller
    std::unique_ptr<LogFilteredData> getNewFilteredData() const;
    // Returns the size if the file in bytes
    int64_t getFileSize() const;
    // Returns the last modification date for the file.
    // std::nullopt if the file is not on disk.
    std::optional<std::chrono::system_clock::time_point> getLastModifiedDate() const;
    // Throw away all the file data and reload/reindex.
    void reload( std::optional<klogg::Encoding> forcedEncoding = std::nullopt );

    // Probe the current file for changes without forcing a full reindex:
    // appends become PartialReindex, truncation becomes FullReindex.
    void checkFileChanges();

    // True while an engine operation (indexing, reload, change check) is
    // executing or queued to run
    bool hasOperationInFlight() const;

    // Get the auto-detected encoding for the indexed text.
    std::optional<klogg::Encoding> getDetectedEncoding() const;

    // Get the encoding forced for the index (via reload/setDisplayEncoding),
    // nullopt when the index uses the auto-detected one.
    std::optional<klogg::Encoding> getForcedEncoding() const;

    // Adds a listener called whenever the loading finishes; listeners are
    // called in registration order, after the main callback. Returns an id
    // to pass to removeLoadingFinishedListener.
    size_t addLoadingFinishedListener( std::function<void( LoadingStatus )> listener );
    void removeLoadingFinishedListener( size_t id );

    struct RawLines {
        LineNumber startLine;

        klogg::vector<char> buffer;
        klogg::vector<int64_t> endOfLines;

        TextDecoder textDecoder;

    public:
        klogg::vector<std::string> decodeLines() const;
        klogg::vector<std::string_view> buildUtf8View() const;

    private:
        mutable klogg::vector<char> utf8Data_;
    };

    RawLines getLinesRaw( LineNumber first, LinesCount number ) const;

    // Batch fetch of an arbitrary set of lines (e.g. the non-contiguous
    // source lines of a filtered projection): one index accessor, file
    // session and decoder for the whole set with a per-line seek+read
    // inside, instead of one full getLines round per line. Returns one
    // string per requested line; per-line failures yield the same warning
    // line the contiguous getLinesFromFile path would pad with.
    klogg::vector<std::string> getLinesForLineNumbers(
        const klogg::vector<LineNumber>& lines ) const;

private:
    // Consider reloading the file when it changes on disk updated
    void fileChangedOnDisk( const std::string& filename );
    // Called when the worker thread signals the current operation ended
    void indexingFinished( LoadingStatus status );
    // Called when the worker thread signals the current operation ended
    void checkFileChangesFinished( MonitoredFileStatus status );

    // Forwarding helper invoked through postTarget_ from the worker thread
    void onProgressed( int percent );

    // Implementation of virtual functions
    std::string doGetLineString( LineNumber line ) const override;
    std::string doGetExpandedLineString( LineNumber line ) const override;
    klogg::vector<std::string> doGetLines( LineNumber first, LinesCount number ) const override;
    klogg::vector<std::string> doGetExpandedLines( LineNumber first,
                                                   LinesCount number ) const override;
    LineNumber doGetLineNumber( LineNumber index ) const override;
    LinesCount doGetNbLine() const override;
    LineLength doGetMaxLength() const override;
    LineLength doGetLineLength( LineNumber line ) const override;
    bool doSetDisplayEncoding( const char* encoding ) override;
    std::optional<klogg::Encoding> doGetDisplayEncoding() const override;
    void doAttachReader() const override;
    void doDetachReader() const override;

    void reOpenFile() const;

    klogg::vector<std::string> getLinesFromFile( LineNumber first, LinesCount number,
                                                 std::string ( *processLine )( std::string&& ) )
        const;

private:
    mutable std::unique_ptr<FileHolder> attached_file_;

    // Indexing data, read by us, written by the worker thread
    std::shared_ptr<IndexingData> indexing_data_;

    OperationQueue operationQueue_;

    std::string indexingFileName_;

    bool keepFileClosed_;

    std::optional<std::chrono::system_clock::time_point> lastModifiedDate_;

    // Codec to decode text
    TextCodecHolder codec_;
    MonitoredFileStatus fileChangedOnDisk_;

    PostTarget postTarget_;
    LogDataCallbacks callbacks_;
    std::shared_ptr<EngineOptionsHolder> optionsHolder_;

    std::map<size_t, std::function<void( LoadingStatus )>> loadingFinishedListeners_;
    size_t nextListenerId_ = 0;

    // Never-reset flag read by the file change observer callback; the control
    // block outlives the LogData, guarding against queued notifications.
    std::shared_ptr<std::atomic<bool>> closingFlag_;
    uint64_t fileWatcherObserverId_ = 0;
};

#endif
