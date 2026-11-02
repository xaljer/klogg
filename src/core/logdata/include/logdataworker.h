/*
 * Copyright (C) 2009, 2010, 2014, 2015 Nicolas Bonnefon and other contributors
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

#ifndef LOGDATAWORKERTHREAD_H
#define LOGDATAWORKERTHREAD_H

#include "containers.h"
#include "filehandle.h"
#include "linetypes.h"
#include <functional>
#include <cstdint>
#include <string>
#include <cstdint>
#include <variant>
#include <cstdint>

#include <tbb/enumerable_thread_specific.h>
#include <cstdint>
#include <tbb/flow_graph.h>
#include <cstdint>
#include <tbb/task_group.h>
#include <cstdint>

#include "atomicflag.h"
#include "filedigest.h"
#include "synchronization.h"

#include "encoding.h"
#include "encodingdetector.h"
#include "engine_options.h"
#include "linepositionarray.h"
#include "loadingstatus.h"

struct IndexedHash {
    int64_t size = 0;
    uint64_t fullDigest = 0;

    int64_t headerSize = 0;
    uint64_t headerDigest = 0;

    int64_t tailSize = 0;
    int64_t tailOffset = 0;
    uint64_t tailDigest = 0;
};

template <typename Data, typename LockGuard>
class IndexingDataAccessor {
public:
    IndexingDataAccessor( Data data )
        : data_( data )
        , guard_( data->dataMutex_ )
    {
    }

    ~IndexingDataAccessor() = default;

    int64_t getIndexedSize() const
    {
        return data_->getIndexedSize();
    }

    IndexedHash getHash() const
    {
        return data_->getHash();
    }

    // Get the length of the longest line
    LineLength getMaxLength() const
    {
        return data_->getMaxLength();
    }

    // Get the total number of lines
    LinesCount getNbLines() const
    {
        return data_->getNbLines();
    }

    // Get the position (in byte from the beginning of the file)
    // of the end of the passed line.
    OffsetInFile getEndOfLineOffset( LineNumber line ) const
    {
        return data_->getEndOfLineOffset( line );
    }

    klogg::vector<OffsetInFile> getEndOfLineOffsets( LineNumber line, LinesCount count ) const
    {
        return data_->getEndOfLineOffsets( line, count );
    }

    // Get the guessed encoding for the content.
    std::optional<klogg::Encoding> getEncodingGuess() const
    {
        return data_->getEncodingGuess();
    }
    void setEncodingGuess( std::optional<klogg::Encoding> codec )
    {
        data_->setEncodingGuess( codec );
    }

    std::optional<klogg::Encoding> getForcedEncoding() const
    {
        return data_->getForcedEncoding();
    }
    void forceEncoding( std::optional<klogg::Encoding> codec )
    {
        return data_->forceEncoding( codec );
    }

    // Atomically add to all the existing
    // indexing data.
    void addAll( const klogg::vector<char>& block, LineLength length,
                 const FastLinePositionArray& linePosition,
                 std::optional<klogg::Encoding> encoding )
    {
        data_->addAll( block, length, linePosition, encoding );
    }

    void setHeaderHash( uint64_t digest, int64_t size )
    {
        data_->hash_.headerSize = size;
        data_->hash_.headerDigest = digest;
    }

    void setTailHash( uint64_t digest, int64_t offset, int64_t size )
    {
        data_->hash_.tailSize = size;
        data_->hash_.tailOffset = offset;
        data_->hash_.tailDigest = digest;
    }

    int getProgress() const
    {
        return data_->getProgress();
    }

    void setProgress( int progress )
    {
        data_->setProgress( progress );
    }

    // Completely clear the indexing data.
    void clear()
    {
        data_->clear();
    }

    size_t allocatedSize() const
    {
        return data_->allocatedSize();
    }

private:
    Data data_;
    LockGuard guard_;
};

// This class is a thread-safe set of indexing data.
class IndexingData {
public:
    using ConstAccessor = IndexingDataAccessor<const IndexingData*, SharedLock>;
    using MutateAccessor = IndexingDataAccessor<IndexingData*, UniqueLock>;

    explicit IndexingData( std::shared_ptr<EngineOptionsHolder> optionsHolder = {} );

    // Current engine options snapshot (live: reflects Session::updateOptions).
    EngineOptions options() const
    {
        return optionsHolder_ ? optionsHolder_->get() : EngineOptions{};
    }

private:
    int64_t getIndexedSize() const;

    IndexedHash getHash() const;

    // Get the length of the longest line
    LineLength getMaxLength() const;

    // Get the total number of lines
    LinesCount getNbLines() const;

    // Get the position (in byte from the beginning of the file)
    // of the end of the passed line.
    OffsetInFile getEndOfLineOffset( LineNumber line ) const;
    klogg::vector<OffsetInFile> getEndOfLineOffsets( LineNumber line, LinesCount count ) const;

    // Get the guessed encoding for the content.
    std::optional<klogg::Encoding> getEncodingGuess() const;
    void setEncodingGuess( std::optional<klogg::Encoding> codec );

    std::optional<klogg::Encoding> getForcedEncoding() const;
    void forceEncoding( std::optional<klogg::Encoding> codec );

    // Atomically add to all the existing
    // indexing data.
    void addAll( const klogg::vector<char>& block, LineLength length,
                 const FastLinePositionArray& linePosition,
                 std::optional<klogg::Encoding> encoding );

    // Completely clear the indexing data.
    void clear();

    size_t allocatedSize() const;

    int getProgress() const;
    void setProgress( int progress );

private:
    mutable SharedMutex dataMutex_;

    using LinePositionArrayType = std::variant<LinePositionArray, FastLinePositionArray>;
    LinePositionArrayType linePosition_;

    LineLength maxLength_;

    int progress_{};

    FileDigest hashBuilder_;
    IndexedHash hash_;

    std::optional<klogg::Encoding> encodingGuess_{};
    std::optional<klogg::Encoding> encodingForced_{};

    bool useFastModificationDetection_ = true;

    std::shared_ptr<EngineOptionsHolder> optionsHolder_;

    friend ConstAccessor;
    friend MutateAccessor;
};

struct IndexingState {

    EncodingParameters encodingParams;
    OffsetInFile::UnderlyingType pos{};
    LineLength::UnderlyingType max_length{};
    LineLength::UnderlyingType additional_spaces{};
    OffsetInFile::UnderlyingType end{};
    OffsetInFile::UnderlyingType file_size{};

    std::optional<klogg::Encoding> encodingGuess{};
    std::optional<klogg::Encoding> fileTextCodec{};
};

using OperationResult = std::variant<bool, MonitoredFileStatus>;

// A process condition the engine cannot recover from and must surface to the
// user; the handler is responsible for marshaling to the UI thread.
enum class EngineErrorKind { TooLongLines, Exception };
using EngineErrorHandler = std::function<void( EngineErrorKind, const std::string& )>;

// A non-Qt set of callbacks an IndexOperation invokes while it runs on the
// worker pool thread.
struct IndexOperationCallbacks {
    std::function<void( int )> onProgress;
    std::function<void( bool )> onIndexingFinished;
    std::function<void( MonitoredFileStatus )> onFileCheckFinished;
    EngineErrorHandler onError;
};

class IndexOperation {
public:
    IndexOperation( const std::string& fileName, const std::shared_ptr<IndexingData>& indexingData,
                    AtomicFlag& interruptRequest, IndexOperationCallbacks callbacks )
        : fileName_( fileName )
        , indexing_data_( indexingData )
        , interruptRequest_( interruptRequest )
        , callbacks_( std::move( callbacks ) )
    {
    }

    // Run the indexing operation, returns true if it has been done
    // and false if it has been cancelled (results not copied)
    virtual ~IndexOperation() = default;
    virtual OperationResult run() = 0;

protected:
    using BlockBuffer = klogg::vector<char>;
    using BlockData = std::pair<OffsetInFile::UnderlyingType, BlockBuffer*>;
    using BlockPrefetcher = tbb::flow::limiter_node<BlockData>;

    // Returns the total size indexed
    // Modify the passed linePosition and maxLength
    void doIndex( OffsetInFile initialPosition );

    std::string fileName_;
    std::shared_ptr<IndexingData> indexing_data_;
    AtomicFlag& interruptRequest_;

    IndexOperationCallbacks callbacks_;

private:
    FastLinePositionArray parseDataBlock( OffsetInFile::UnderlyingType blockBegining,
                                          const BlockBuffer& block, IndexingState& state ) const;

    void guessEncoding( const BlockBuffer& block, IndexingData::MutateAccessor& scopedAccessor,
                        IndexingState& state ) const;

    std::chrono::microseconds readFileInBlocks( klogg::FileHandle& file, uint64_t offset,
                                                uint64_t fileSize, uint64_t& endPos,
                                                BlockPrefetcher& blockPrefetcher );
    void indexNextBlock( IndexingState& state, const BlockData& blockData );
};

class FullIndexOperation : public IndexOperation {
public:
    FullIndexOperation( const std::string& fileName,
                        const std::shared_ptr<IndexingData>& indexingData,
                        AtomicFlag& interruptRequest, IndexOperationCallbacks callbacks,
                        std::optional<klogg::Encoding> forcedEncoding = std::nullopt )
        : IndexOperation( fileName, indexingData, interruptRequest, std::move( callbacks ) )
        , forcedEncoding_( forcedEncoding )
    {
    }
    OperationResult run() override;

private:
    std::optional<klogg::Encoding> forcedEncoding_;
};

class PartialIndexOperation : public IndexOperation {
public:
    PartialIndexOperation( const std::string& fileName,
                           const std::shared_ptr<IndexingData>& indexingData,
                           AtomicFlag& interruptRequest, IndexOperationCallbacks callbacks )
        : IndexOperation( fileName, indexingData, interruptRequest, std::move( callbacks ) )
    {
    }

    OperationResult run() override;
};

class CheckFileChangesOperation : public IndexOperation {
public:
    CheckFileChangesOperation( const std::string& fileName,
                               const std::shared_ptr<IndexingData>& indexingData,
                               AtomicFlag& interruptRequest, IndexOperationCallbacks callbacks )
        : IndexOperation( fileName, indexingData, interruptRequest, std::move( callbacks ) )
    {
    }

    OperationResult run() override;

private:
    MonitoredFileStatus doCheckFileChanges();
};

// The worker-side notifications LogData cares about. All of them fire on the
// worker pool thread; the consumer decides whether to forward them.
struct LogDataWorkerCallbacks {
    std::function<void( int )> onProgress;
    std::function<void( LoadingStatus )> onIndexingFinished;
    std::function<void( MonitoredFileStatus )> onFileCheckFinished;
};

class LogDataWorker {
public:
    // Pass a pointer to the IndexingData (initially empty)
    // This object will change it when indexing (IndexingData must be thread safe!)
    LogDataWorker( const std::shared_ptr<IndexingData>& indexing_data,
                   LogDataWorkerCallbacks callbacks, EngineErrorHandler errorHandler,
                   std::shared_ptr<EngineOptionsHolder> optionsHolder = {} );
    ~LogDataWorker();

    LogDataWorker( const LogDataWorker& ) = delete;
    LogDataWorker& operator=( const LogDataWorker&& ) = delete;

    LogDataWorker( LogDataWorker&& ) = delete;
    LogDataWorker& operator=( LogDataWorker&& ) = delete;

    // Attaches to a file on disk. Attaching to a non existant file
    // will work, it will just appear as an empty file.
    void attachFile( const std::string& fileName );
    // Instructs the thread to start a new full indexing of the file, sending
    // signals as it progresses.
    void indexAll( std::optional<klogg::Encoding> forcedEncoding = std::nullopt );
    // Instructs the thread to start a partial indexing (starting at
    // the end of the file as indexed).
    void indexAdditionalLines();

    void checkFileChanges();

    // Interrupts the indexing if one is in progress
    void interrupt();

    // Current engine options snapshot (live: reflects Session::updateOptions).
    EngineOptions options() const
    {
        return optionsHolder_ ? optionsHolder_->get() : EngineOptions{};
    }

private:
    IndexOperationCallbacks operationCallbacks() const;

    void onIndexingFinished( bool result ) const;
    void onCheckFileFinished( MonitoredFileStatus result ) const;

    // Mutex to wait for operations
    tbb::task_group operationsPool_;
    Mutex operationsMutex_;
    AtomicFlag interruptRequest_;

    std::string fileName_;

    // Pointer to the owner's indexing data (we modify it)
    std::shared_ptr<IndexingData> indexing_data_;

    LogDataWorkerCallbacks callbacks_;
    EngineErrorHandler errorHandler_;
    std::shared_ptr<EngineOptionsHolder> optionsHolder_;
};

#endif
