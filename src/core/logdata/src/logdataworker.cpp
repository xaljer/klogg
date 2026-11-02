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

#include <chrono>
#include <cstdint>
#include <exception>
#include <cstdint>
#include <filesystem>
#include <cstdint>
#include <functional>
#include <cstdint>
#include <string_view>
#include <cstdint>
#include <system_error>
#include <cstdint>
#include <thread>
#include <cstdint>

#include <tuple>
#include <cstdint>

#include "containers.h"
#include "encodingdetector.h"
#include "linepositionarray.h"
#include "log.h"
#include "logdata.h"
#include "paths.h"
#include "progress.h"
#include "readablesize.h"

#include "logdataworker.h"

constexpr int IndexingBlockSize = 5 * 1024 * 1024;

int64_t IndexingData::getIndexedSize() const
{
    return hash_.size;
}

IndexedHash IndexingData::getHash() const
{
    return hash_;
}

LineLength IndexingData::getMaxLength() const
{
    return maxLength_;
}

LinesCount IndexingData::getNbLines() const
{
    return LinesCount( std::visit( []( const auto& linePosition ) { return linePosition.size(); },
                                   linePosition_ ) );
}

OffsetInFile IndexingData::getEndOfLineOffset( LineNumber line ) const
{
    return std::visit(
        [ line ]( const auto& linePosition ) { return linePosition.at( line.get() ); },
        linePosition_ );
}

klogg::vector<OffsetInFile> IndexingData::getEndOfLineOffsets( LineNumber line,
                                                               LinesCount count ) const
{
    return std::visit(
        [ line, count ]( const auto& linePosition ) { return linePosition.range( line, count ); },
        linePosition_ );
}

std::optional<klogg::Encoding> IndexingData::getEncodingGuess() const
{
    return encodingGuess_;
}

void IndexingData::setEncodingGuess( std::optional<klogg::Encoding> codec )
{
    encodingGuess_ = codec;
}

void IndexingData::forceEncoding( std::optional<klogg::Encoding> codec )
{
    encodingForced_ = codec;
}

std::optional<klogg::Encoding> IndexingData::getForcedEncoding() const
{
    return encodingForced_;
}

void IndexingData::addAll( const klogg::vector<char>& block, LineLength length,
                           const FastLinePositionArray& newLinePosition,
                           std::optional<klogg::Encoding> encoding )

{
    maxLength_ = std::max( maxLength_, length );
    std::visit(
        [ &newLinePosition ]( auto& linePosition ) { linePosition.append_list( newLinePosition ); },
        linePosition_ );

    if ( !block.empty() ) {
        hash_.size += klogg::ssize( block );

        if ( !useFastModificationDetection_ ) {
            hashBuilder_.addData( block.data(), block.size() );
            hash_.fullDigest = hashBuilder_.digest();
        }
    }

    encodingGuess_ = encoding;
}

int IndexingData::getProgress() const
{
    return progress_;
}

void IndexingData::setProgress( int progress )
{
    progress_ = progress;
}

IndexingData::IndexingData( std::shared_ptr<EngineOptionsHolder> optionsHolder )
    : optionsHolder_( std::move( optionsHolder ) )
{
}

void IndexingData::clear()
{
    const auto engineOptions = options();

    maxLength_ = 0_length;
    hash_ = {};
    hashBuilder_.reset();
    // The aliases read backwards from what they hold: LinePositionArray wraps
    // the compressed storage and FastLinePositionArray the simple
    // (uncompressed, faster) one, so the compressed-index preference
    // selects LinePositionArray
    if ( engineOptions.useCompressedIndex ) {
        linePosition_ = LinePositionArrayType( LinePositionArray{} );
    }
    else {
        linePosition_ = LinePositionArrayType( FastLinePositionArray{} );
    }
    encodingGuess_ = std::nullopt;
    encodingForced_ = std::nullopt;

    progress_ = {};
    useFastModificationDetection_ = engineOptions.fastModificationDetection;
}

size_t IndexingData::allocatedSize() const
{
    return std::visit( []( const auto& linePosition ) { return linePosition.allocatedSize(); },
                       linePosition_ );
}

LogDataWorker::LogDataWorker( const std::shared_ptr<IndexingData>& indexing_data,
                              LogDataWorkerCallbacks callbacks, EngineErrorHandler errorHandler,
                              std::shared_ptr<EngineOptionsHolder> optionsHolder )
    : indexing_data_( indexing_data )
    , callbacks_( std::move( callbacks ) )
    , errorHandler_( std::move( errorHandler ) )
    , optionsHolder_( std::move( optionsHolder ) )
{
}

LogDataWorker::~LogDataWorker()
{
    try {
        interruptRequest_.set();
        // A task_group task locks operationsMutex_ while it runs, so holding
        // the mutex across wait() would deadlock with a task that has been
        // spawned but not finished yet. The brief lock only serializes with
        // in-flight index requests; the interrupt flag makes any pending
        // operation stop.
        {
            ScopedLock locker( operationsMutex_ );
        }
        operationsPool_.wait();
        LOG_INFO << "LogDataWorker shutdown";
    } catch ( const std::exception& e ) {
        LOG_ERROR << "Failed to destroy LogDataWorker: " << e.what();
    }
}

void LogDataWorker::attachFile( const std::string& fileName )
{
    ScopedLock locker( operationsMutex_ );
    interruptRequest_.clear();
    fileName_ = fileName;
}

void LogDataWorker::indexAll( std::optional<klogg::Encoding> forcedEncoding )
{
    // Wait outside the lock: a spawned task acquires it on start
    // (see ~LogDataWorker)
    operationsPool_.wait();
    ScopedLock locker( operationsMutex_ );
    interruptRequest_.clear();

    LOG_INFO << "FullIndex requested, forced encoding: "
             << ( forcedEncoding ? forcedEncoding->name : std::string{ "none" } );
    operationsPool_.run( [ this, forcedEncoding, fileName = fileName_ ] {
        LOG_INFO << "FullIndex thread started";
        ScopedLock operationLock( operationsMutex_ );
        auto operationRequested = std::make_unique<FullIndexOperation>(
            fileName, indexing_data_, interruptRequest_, operationCallbacks(), forcedEncoding );
        return operationRequested->run();
    } );
}

void LogDataWorker::indexAdditionalLines()
{
    // Wait outside the lock: a spawned task acquires it on start
    // (see ~LogDataWorker)
    operationsPool_.wait();
    ScopedLock locker( operationsMutex_ );
    interruptRequest_.clear();

    LOG_INFO << "PartialIndex requested";

    operationsPool_.run( [ this, fileName = fileName_ ] {
        LOG_INFO << "PartialIndex thread started";
        ScopedLock operationLock( operationsMutex_ );
        auto operationRequested = std::make_unique<PartialIndexOperation>( fileName, indexing_data_,
                                                                           interruptRequest_,
                                                                           operationCallbacks() );
        return operationRequested->run();
    } );
}

void LogDataWorker::checkFileChanges()
{
    // Wait outside the lock: a spawned task acquires it on start
    // (see ~LogDataWorker)
    operationsPool_.wait();
    ScopedLock locker( operationsMutex_ );
    interruptRequest_.clear();

    LOG_INFO << "Check file changes requested";

    operationsPool_.run( [ this, fileName = fileName_ ] {
        ScopedLock operationLock( operationsMutex_ );
        auto operationRequested = std::make_unique<CheckFileChangesOperation>(
            fileName, indexing_data_, interruptRequest_, operationCallbacks() );

        return operationRequested->run();
    } );
}

IndexOperationCallbacks LogDataWorker::operationCallbacks() const
{
    return IndexOperationCallbacks{ callbacks_.onProgress,
                                    [ this ]( bool result ) { onIndexingFinished( result ); },
                                    [ this ]( MonitoredFileStatus result ) {
                                        onCheckFileFinished( result );
                                    },
                                    errorHandler_ };
}

void LogDataWorker::interrupt()
{
    LOG_INFO << "Load interrupt requested";
    interruptRequest_.set();
}

void LogDataWorker::onIndexingFinished( bool result ) const
{
    if ( result ) {
        LOG_INFO << "finished indexing in worker thread";
        callbacks_.onIndexingFinished( LoadingStatus::Successful );
    }
    else {
        LOG_INFO << "indexing interrupted in worker thread";
        callbacks_.onIndexingFinished( LoadingStatus::Interrupted );
    }
}

void LogDataWorker::onCheckFileFinished( const MonitoredFileStatus result ) const
{
    LOG_INFO << "checking file finished in worker thread";
    callbacks_.onFileCheckFinished( result );
}

//
// Operations implementation
//
namespace parse_data_block {

std::string_view::size_type findNextMultiByteDelimeter( EncodingParameters encodingParams,
                                                        std::string_view data, char delimeter )
{
    auto nextDelimeter = data.find( delimeter );

    if ( nextDelimeter == std::string_view::npos ) {
        return nextDelimeter;
    }

    const auto isNotDelimeter = [ &encodingParams, data ]( std::string_view::size_type checkPos ) {
        const auto lineFeedWidth
            = static_cast<std::string_view::size_type>( encodingParams.lineFeedWidth );

        const auto isCheckForward = encodingParams.lineFeedIndex == 0;

        if ( isCheckForward && checkPos + lineFeedWidth > data.size() ) {
            return true;
        }
        else if ( !isCheckForward && checkPos < lineFeedWidth - 1 ) {
            return true;
        }

        for ( auto i = 1u; i < lineFeedWidth; ++i ) {
            const auto nextByte = isCheckForward ? data[ checkPos + i ] : data[ checkPos - i ];
            if ( nextByte != '\0' ) {
                return true;
            }
        }

        return false;
    };

    while ( nextDelimeter != std::string_view::npos && isNotDelimeter( nextDelimeter ) ) {
        nextDelimeter = data.find( delimeter, nextDelimeter + 1 );
    }

    return nextDelimeter;
}

std::string_view::size_type findNextSingleByteDelimeter( EncodingParameters, std::string_view data,
                                                         char delimeter )
{
    return data.find( delimeter );
}

int charOffsetWithinBlock( const char* blockStart, const char* pointer,
                           const EncodingParameters& encodingParams )
{
    return type_safe::narrow_cast<int>( std::distance( blockStart, pointer ) )
           - encodingParams.getBeforeCrOffset();
}

using FindDelimeter = std::string_view::size_type ( * )( EncodingParameters encodingParams,
                                                         std::string_view, char );

LineLength::UnderlyingType
expandTabsInLine( const klogg::vector<char>& block, std::string_view blockToExpand,
                  int posWithinBlock, EncodingParameters encodingParams,
                  FindDelimeter findNextDelimeter,
                  LineLength::UnderlyingType initialAdditionalSpaces = 0 )
{
    auto additionalSpaces = initialAdditionalSpaces;
    while ( !blockToExpand.empty() ) {
        const auto nextTab = findNextDelimeter( encodingParams, blockToExpand, '\t' );
        if ( nextTab == std::string_view::npos ) {
            break;
        }

        const auto tabPosWithinBlock
            = charOffsetWithinBlock( block.data(), blockToExpand.data() + nextTab, encodingParams );

        LOG_DEBUG << "Tab at " << tabPosWithinBlock;

        const auto currentExpandedSize = tabPosWithinBlock - posWithinBlock + additionalSpaces;

        additionalSpaces += TabStop - ( currentExpandedSize % TabStop ) - 1;
        if ( nextTab >= blockToExpand.size() ) {
            break;
        }

        blockToExpand.remove_prefix( nextTab + 1 );
    }

    return additionalSpaces;
}

std::tuple<bool, int, LineLength::UnderlyingType>
findNextLineFeed( const klogg::vector<char>& block, int posWithinBlock, const IndexingState& state,
                  FindDelimeter findNextDelimeter )
{
    const auto searchStart = block.data() + posWithinBlock;
    const auto searchLineSize = static_cast<size_t>( klogg::ssize( block ) - posWithinBlock );

    const auto blockView = std::string_view( searchStart, searchLineSize );
    const auto nextLineFeed = findNextDelimeter( state.encodingParams, blockView, '\n' );

    const auto isEndOfBlock = nextLineFeed == std::string_view::npos;
    const auto nextLineSize = !isEndOfBlock ? nextLineFeed : searchLineSize;

    posWithinBlock
        = charOffsetWithinBlock( block.data(), searchStart + nextLineSize, state.encodingParams );

    const auto additionalSpaces
        = expandTabsInLine( block, blockView.substr( 0, nextLineSize ), posWithinBlock,
                            state.encodingParams, findNextDelimeter, state.additional_spaces );

    return std::make_tuple( isEndOfBlock, posWithinBlock, additionalSpaces );
}
} // namespace parse_data_block

FastLinePositionArray IndexOperation::parseDataBlock( OffsetInFile::UnderlyingType blockBeginning,
                                                      const klogg::vector<char>& block,
                                                      IndexingState& state ) const
{
    using namespace parse_data_block;

    FindDelimeter findNextDelimeter;
    if ( state.encodingParams.lineFeedWidth == 1 ) {
        findNextDelimeter = findNextSingleByteDelimeter;
    }
    else {
        findNextDelimeter = findNextMultiByteDelimeter;
    }

    bool isEndOfBlock = false;
    FastLinePositionArray linePositions;

    while ( !isEndOfBlock ) {
        if ( state.pos > blockBeginning + klogg::ssize( block ) ) {
            LOG_ERROR << "Trying to parse out of block: " << state.pos << " " << blockBeginning
                      << " " << block.size();
            break;
        }

        auto posWithinBlock = type_safe::narrow_cast<int>(
            state.pos >= blockBeginning ? ( state.pos - blockBeginning ) : 0 );

        isEndOfBlock = posWithinBlock == klogg::ssize( block );

        if ( !isEndOfBlock ) {
            std::tie( isEndOfBlock, posWithinBlock, state.additional_spaces )
                = findNextLineFeed( block, posWithinBlock, state, findNextDelimeter );
        }

        const auto currentDataEnd = posWithinBlock + blockBeginning;

        const auto length
            = type_safe::narrow_cast<LineLength::UnderlyingType>( currentDataEnd - state.pos )
                  / state.encodingParams.lineFeedWidth
              + state.additional_spaces;

        state.max_length = std::max( state.max_length, length );

        if ( !isEndOfBlock ) {
            state.end = currentDataEnd;
            state.pos = state.end + state.encodingParams.lineFeedWidth;
            state.additional_spaces = 0;
            linePositions.append( OffsetInFile( state.pos ) );
        }
    }

    return linePositions;
}

void IndexOperation::guessEncoding( const klogg::vector<char>& block,
                                    IndexingData::MutateAccessor& scopedAccessor,
                                    IndexingState& state ) const
{
    if ( !state.encodingGuess ) {
        state.encodingGuess = EncodingDetector::getInstance().detectEncoding( block );
        LOG_INFO << "Encoding guess " << state.encodingGuess->name;
    }

    if ( !state.fileTextCodec ) {
        state.fileTextCodec = scopedAccessor.getForcedEncoding();

        if ( !state.fileTextCodec ) {
            state.fileTextCodec = scopedAccessor.getEncodingGuess();
        }

        if ( !state.fileTextCodec ) {
            state.fileTextCodec = state.encodingGuess;
        }
    }

    state.encodingParams = EncodingParameters( *state.fileTextCodec );

    LOG_DEBUG << "Encoding " << state.fileTextCodec->name << ", Char width "
              << state.encodingParams.lineFeedWidth;
}

std::chrono::microseconds IndexOperation::readFileInBlocks( klogg::FileHandle& file,
                                                            uint64_t offset, uint64_t fileSize,
                                                            uint64_t& endPos,
                                                            BlockPrefetcher& blockPrefetcher )
{
    using namespace std::chrono;
    using clock = high_resolution_clock;

    LOG_INFO << "Starting IO thread";

    int sentBlocksCount = 0;

    microseconds ioDuration{};
    auto currentPos = offset;
    while ( currentPos < fileSize ) {

        if ( interruptRequest_ ) {
            break;
        }

        BlockData blockData{ static_cast<OffsetInFile::UnderlyingType>( currentPos ),
                             new klogg::vector<char>( IndexingBlockSize ) };

        clock::time_point ioT1 = clock::now();
        const auto readBytes
            = file.readAt( currentPos, blockData.second->data(), blockData.second->size() );

        if ( readBytes == 0 ) {
            delete blockData.second;
            break;
        }

        if ( readBytes < blockData.second->size() ) {
            blockData.second->resize( readBytes );
        }

        currentPos += readBytes;

        clock::time_point ioT2 = clock::now();

        ioDuration += duration_cast<microseconds>( ioT2 - ioT1 );

        if ( sentBlocksCount % 10 == 0 ) {
            LOG_INFO << "Sending block " << blockData.first << " size " << blockData.second->size();
        }

        while ( !blockPrefetcher.try_put( std::move( blockData ) ) && !interruptRequest_ ) {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        sentBlocksCount++;
    }

    endPos = currentPos;

    auto lastBlock = std::make_pair( -1, new klogg::vector<char>{} );
    while ( !blockPrefetcher.try_put( lastBlock ) && !interruptRequest_ ) {
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }

    LOG_INFO << "IO thread done";
    return ioDuration;
}

void IndexOperation::indexNextBlock( IndexingState& state, const BlockData& blockData )
{
    const auto& blockBeginning = blockData.first;
    const auto& block = *blockData.second;

    LOG_DEBUG << "Indexing block " << blockBeginning << " start";

    if ( blockBeginning < 0 ) {
        return;
    }

    IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };

    guessEncoding( block, scopedAccessor, state );

    if ( !block.empty() ) {
        const auto linePositions = parseDataBlock( blockBeginning, block, state );
        auto maxLength = state.max_length;
        if ( maxLength > std::numeric_limits<LineLength::UnderlyingType>::max() ) {
            LOG_ERROR << "Too long lines " << maxLength;
            maxLength = std::numeric_limits<LineLength::UnderlyingType>::max();
        }

        scopedAccessor.addAll(
            block, LineLength( type_safe::narrow_cast<LineLength::UnderlyingType>( maxLength ) ),
            linePositions, state.encodingGuess );

        // Update the caller for progress indication
        const auto progress
            = ( state.file_size > 0 ) ? calculateProgress( state.pos, state.file_size ) : 100;

        if ( progress != scopedAccessor.getProgress() ) {
            scopedAccessor.setProgress( progress );
            LOG_DEBUG << "Indexing progress " << progress << ", indexed size " << state.pos;
            callbacks_.onProgress( progress );
        }
    }
    else {
        scopedAccessor.setEncodingGuess( state.encodingGuess );
    }

    LOG_DEBUG << "Indexing block " << blockBeginning << " done";
}

void IndexOperation::doIndex( OffsetInFile initialPosition )
{
    LOG_INFO << "Indexing file " << fileName_;
    klogg::FileHandle file;
    if ( !file.open( fileName_ ) ) {
        // TODO: Check that the file is seekable?
        // If the file cannot be open, we do as if it was empty
        LOG_WARNING << "Cannot open file " << fileName_;

        IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };

        scopedAccessor.clear();
        scopedAccessor.setEncodingGuess( klogg::enc::defaultEncoding() );

        scopedAccessor.setProgress( 100 );
        callbacks_.onProgress( 100 );
        return;
    }

    const auto fileSize = static_cast<OffsetInFile::UnderlyingType>( file.size().value_or( 0 ) );
    LOG_INFO << "File size " << fileSize;

    IndexingState state;
    state.pos = initialPosition.get();
    state.file_size = fileSize;

    {
        IndexingData::ConstAccessor scopedAccessor{ indexing_data_.get() };

        state.fileTextCodec = scopedAccessor.getForcedEncoding();
        if ( !state.fileTextCodec ) {
            state.fileTextCodec = scopedAccessor.getEncodingGuess();
        }

        state.encodingGuess = scopedAccessor.getEncodingGuess();
        LOG_INFO << "Initial encoding "
                 << ( state.fileTextCodec ? state.fileTextCodec->name : std::string{ "auto" } );
    }

    const auto prefetchBufferSize
        = static_cast<size_t>( indexing_data_->options().indexReadBufferSizeMb );

    LOG_INFO << "Prefetch buffer " << readableSize( prefetchBufferSize * IndexingBlockSize );

    using namespace std::chrono;
    using clock = high_resolution_clock;
    microseconds ioDuration{};

    const auto indexingStartTime = clock::now();

    tbb::flow::graph indexingGraph;
    auto blockPrefetcher = tbb::flow::limiter_node<BlockData>( indexingGraph, prefetchBufferSize );
    auto blockQueue = tbb::flow::queue_node<BlockData>( indexingGraph );

    auto blockParser = tbb::flow::function_node<BlockData, tbb::flow::continue_msg>(
        indexingGraph, tbb::flow::serial, [ this, &state ]( const BlockData& blockData ) {
            indexNextBlock( state, blockData );
            delete blockData.second;
            return tbb::flow::continue_msg{};
        } );

    tbb::flow::make_edge( blockPrefetcher, blockQueue );
    tbb::flow::make_edge( blockQueue, blockParser );
    tbb::flow::make_edge( blockParser, blockPrefetcher.decrementer() );

    uint64_t endFilePos = 0;
    ioDuration = readFileInBlocks( file, static_cast<uint64_t>( state.pos ),
                                   static_cast<uint64_t>( fileSize ), endFilePos, blockPrefetcher );
    indexingGraph.wait_for_all();

    IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };

    LOG_DEBUG << "Indexed up to " << state.pos;

    // Check if there is a non LF terminated line at the end of the file
    if ( !interruptRequest_ && state.file_size > state.pos ) {
        LOG_WARNING << "Non LF terminated file, adding a fake end of line";

        FastLinePositionArray line_position;
        line_position.append( OffsetInFile( state.file_size + 1 ) );
        line_position.setFakeFinalLF();

        scopedAccessor.addAll( {}, 0_length, line_position, state.encodingGuess );
    }

    klogg::vector<char> hashBuffer( IndexingBlockSize );
    const auto headerHashSize
        = static_cast<int64_t>( file.readAt( 0, hashBuffer.data(), hashBuffer.size() ) );
    FileDigest fastHashDigest;
    fastHashDigest.addData( hashBuffer.data(), static_cast<size_t>( headerHashSize ) );

    scopedAccessor.setHeaderHash( fastHashDigest.digest(), headerHashSize );

    if ( endFilePos <= hashBuffer.size() ) {
        scopedAccessor.setTailHash( fastHashDigest.digest(), 0, headerHashSize );
    }
    else {
        const auto tailHashOffset = endFilePos - hashBuffer.size();
        const auto tailHashSize = static_cast<int64_t>( file.readAt(
            static_cast<uint64_t>( tailHashOffset ), hashBuffer.data(), hashBuffer.size() ) );
        fastHashDigest.reset();
        fastHashDigest.addData( hashBuffer.data(), static_cast<size_t>( tailHashSize ) );
        scopedAccessor.setTailHash( fastHashDigest.digest(), static_cast<int64_t>( tailHashOffset ),
                                    tailHashSize );
    }

    const auto indexingEndTime = high_resolution_clock::now();
    const auto duration = duration_cast<microseconds>( indexingEndTime - indexingStartTime );

    LOG_INFO << "Indexing done, took " << duration.count() << "us, io " << ioDuration.count() << "us";
    LOG_INFO << "Index size "
             << readableSize( static_cast<uint64_t>( scopedAccessor.allocatedSize() ) );
    LOG_INFO << "Indexed lines " << scopedAccessor.getNbLines();
    LOG_INFO << "Max line " << scopedAccessor.getMaxLength();
    LOG_INFO << "Indexing perf "
             << ( 1000.f * 1000.f * static_cast<float>( state.file_size )
                  / static_cast<float>( duration.count() ) )
                    / ( 1024 * 1024 )
             << " MiB/s";
    if ( interruptRequest_ ) {
        scopedAccessor.clear();
    }

    if ( scopedAccessor.getMaxLength().get()
         == std::numeric_limits<LineLength::UnderlyingType>::max() ) {
        if ( callbacks_.onError ) {
            callbacks_.onError( EngineErrorKind::TooLongLines,
                               "Can't index file: some lines are too long" );
        }

        scopedAccessor.clear();
    }

    if ( !scopedAccessor.getEncodingGuess() ) {
        scopedAccessor.setEncodingGuess( klogg::enc::defaultEncoding() );
    }
}

// Called in the worker thread's context
OperationResult FullIndexOperation::run()
{
    try {
        LOG_INFO << "FullIndexOperation::run(), file " << fileName_;

        callbacks_.onProgress( 0 );

        {
            IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };
            scopedAccessor.clear();
            scopedAccessor.forceEncoding( forcedEncoding_ );
        }

        doIndex( 0_offset );

        LOG_INFO << "FullIndexOperation: ... finished, interrupt = "
                 << static_cast<bool>( interruptRequest_ );

        const auto result = interruptRequest_ ? false : true;
        callbacks_.onIndexingFinished( result );
        return result;
    } catch ( const std::exception& err ) {
        const auto errorString = std::string( "FullIndexOperation failed: " ) + err.what();
        LOG_ERROR << errorString;
        if ( callbacks_.onError ) {
            callbacks_.onError( EngineErrorKind::Exception, errorString );
        }

        {
            IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };
            scopedAccessor.clear();
        }

        callbacks_.onIndexingFinished( false );
        return false;
    }
}

OperationResult PartialIndexOperation::run()
{
    try {
        LOG_INFO << "PartialIndexOperation::run(), file " << fileName_;

        const auto initialPosition
            = OffsetInFile( IndexingData::ConstAccessor{ indexing_data_.get() }.getIndexedSize() );

        LOG_INFO << "PartialIndexOperation: Starting the count at " << initialPosition << " ...";

        callbacks_.onProgress( 0 );

        doIndex( initialPosition );

        LOG_INFO << "PartialIndexOperation: ... finished counting.";

        const auto result = interruptRequest_ ? false : true;
        callbacks_.onIndexingFinished( result );
        return result;
    } catch ( const std::exception& err ) {
        const auto errorString = std::string( "PartialIndexOperation failed: " ) + err.what();
        LOG_ERROR << errorString;
        if ( callbacks_.onError ) {
            callbacks_.onError( EngineErrorKind::Exception, errorString );
        }

        {
            IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };
            scopedAccessor.clear();
        }

        callbacks_.onIndexingFinished( false );
        return false;
    }
}

OperationResult CheckFileChangesOperation::run()
{
    try {
        LOG_INFO << "CheckFileChangesOperation::run(), file " << fileName_;
        const auto result = doCheckFileChanges();
        callbacks_.onFileCheckFinished( result );
        return result;
    } catch ( const std::exception& err ) {
        const auto errorString
            = std::string( "CheckFileChangesOperation failed: " ) + err.what();
        LOG_ERROR << errorString;
        if ( callbacks_.onError ) {
            callbacks_.onError( EngineErrorKind::Exception, errorString );
        }
        callbacks_.onFileCheckFinished( MonitoredFileStatus::Truncated );
        return MonitoredFileStatus::Truncated;
    }
}

MonitoredFileStatus CheckFileChangesOperation::doCheckFileChanges()
{
    const auto indexedHash = IndexingData::ConstAccessor{ indexing_data_.get() }.getHash();

    std::error_code statErrorCode;
    const auto realFileSize
        = std::filesystem::file_size( platform::paths::pathFromUtf8( fileName_ ), statErrorCode );
    if ( statErrorCode ) {
        LOG_INFO << "File stat failed";
        return MonitoredFileStatus::Truncated;
    }

    if ( realFileSize == 0 || realFileSize < static_cast<uintmax_t>( indexedHash.size ) ) {
        LOG_INFO << "File truncated";
        return MonitoredFileStatus::Truncated;
    }
    else {
        klogg::FileHandle file;
        if ( !file.open( fileName_ ) ) {
            LOG_INFO << "File failed to open";
            return MonitoredFileStatus::Truncated;
        }

        klogg::vector<char> buffer( IndexingBlockSize );

        bool isFileModified = false;

        const auto getDigest = [ &file, &buffer ]( const uint64_t startOffset,
                                                   const uint64_t indexedSize ) {
            FileDigest fileDigest;
            auto totalSize = 0ull;
            while ( totalSize < indexedSize ) {
                const auto bytesToRead
                    = std::min( static_cast<size_t>( indexedSize - totalSize ), buffer.size() );
                const auto readSize
                    = file.readAt( startOffset + totalSize, buffer.data(), bytesToRead );
                if ( readSize == 0 ) {
                    break;
                }
                fileDigest.addData( buffer.data(), readSize );
                totalSize += readSize;
            }
            return fileDigest.digest();
        };
        if ( indexing_data_->options().fastModificationDetection ) {
            const auto headerDigest
                = getDigest( 0, static_cast<uint64_t>( indexedHash.headerSize ) );

            LOG_INFO << "indexed header xxhash " << indexedHash.headerDigest;
            LOG_INFO << "current header xxhash " << headerDigest << ", size "
                     << indexedHash.headerSize;

            isFileModified = headerDigest != indexedHash.headerDigest;

            if ( !isFileModified ) {
                const auto tailDigest = getDigest( static_cast<uint64_t>( indexedHash.tailOffset ),
                                                   static_cast<uint64_t>( indexedHash.tailSize ) );

                LOG_INFO << "indexed tail xxhash " << indexedHash.tailDigest;
                LOG_INFO << "current tail xxhash " << tailDigest << ", size "
                         << indexedHash.tailSize;

                isFileModified = tailDigest != indexedHash.tailDigest;
            }
        }
        else {

            const auto realHashDigest = getDigest( 0, static_cast<uint64_t>( indexedHash.size ) );

            LOG_INFO << "indexed xxhash " << indexedHash.fullDigest;
            LOG_INFO << "current xxhash " << realHashDigest;

            isFileModified = realHashDigest != indexedHash.fullDigest;
        }

        if ( isFileModified ) {
            LOG_INFO << "File changed in indexed range";
            return MonitoredFileStatus::Truncated;
        }
        else if ( realFileSize > static_cast<uintmax_t>( indexedHash.size ) ) {
            LOG_INFO << "New data on disk";
            return MonitoredFileStatus::DataAdded;
        }
        else {
            LOG_INFO << "No change in file";
            return MonitoredFileStatus::Unchanged;
        }
    }
}
