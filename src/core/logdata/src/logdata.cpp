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

// This file implements LogData, the content of a log file.

#include <algorithm>
#include <cstdint>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <cstdint>
#include <limits>
#include <cstdint>
#include <numeric>
#include <cstdint>
#include <optional>
#include <cstdint>
#include <string_view>
#include <cstdint>
#include <system_error>
#include <cstdint>
#include <utility>
#include <cstdint>
#include <vector>
#include <cstdint>

#include <simdutf.h>
#include <cstdint>

#include "containers.h"
#include "log.h"
#include "logfiltereddata.h"
#include "paths.h"

#include "logdata.h"

LogData::LogData( PostTarget postTarget, LogDataCallbacks callbacks,
                  std::shared_ptr<EngineOptionsHolder> optionsHolder )
    : AbstractLogData()
    , indexing_data_( nullptr )
    , operationQueue_( [ this ] { attached_file_->attachReader(); } )
    , codec_( klogg::enc::defaultEncoding() )
    , postTarget_( std::move( postTarget ) )
    , callbacks_( std::move( callbacks ) )
    , optionsHolder_( std::move( optionsHolder ) )
{
    if ( !optionsHolder_ ) {
        optionsHolder_ = std::make_shared<EngineOptionsHolder>();
    }
    indexing_data_ = std::make_shared<IndexingData>( optionsHolder_ );

    // The guard outlives the LogData, so a notification already queued on
    // postTarget_ cannot reach a destroyed object.
    closingFlag_ = std::make_shared<std::atomic<bool>>( false );
    fileWatcherObserverId_ = FileWatcher::getFileWatcher().addObserver(
        [ this, guard = closingFlag_ ]( const std::string& fileName ) {
            if ( guard->load() ) {
                return;
            }
            fileChangedOnDisk( fileName );
        },
        postTarget_ );

    LogDataWorkerCallbacks workerCallbacks;
    // The worker runs on a pool thread; post the completion back to the
    // LogData owner thread, never run it inline (would deadlock the queue).
    workerCallbacks.onProgress = [ this ]( int p ) {
        postTarget_( [ this, p ] { onProgressed( p ); } );
    };
    workerCallbacks.onIndexingFinished = [ this ]( LoadingStatus s ) {
        postTarget_( [ this, s ] { indexingFinished( s ); } );
    };
    workerCallbacks.onFileCheckFinished = [ this ]( MonitoredFileStatus s ) {
        postTarget_( [ this, s ] { checkFileChangesFinished( s ); } );
    };

    auto worker
        = std::make_unique<LogDataWorker>( indexing_data_, std::move( workerCallbacks ),
                                           callbacks_.onError, optionsHolder_ );

    operationQueue_.setWorker( std::move( worker ) );

    const auto options = optionsHolder_->get();
    keepFileClosed_ = options.keepFileClosed;

    if ( keepFileClosed_ ) {
        LOG_INFO << "Keep file closed option is set";
    }

    const auto defaultEncodingMib = options.defaultEncodingMib;
    if ( defaultEncodingMib >= 0 ) {
        codec_.setCodec( klogg::enc::fromMib( defaultEncodingMib ) );
    }
}

LogData::~LogData()
{
    LOG_DEBUG << "Destroying log data";
    closingFlag_->store( true );
    FileWatcher::getFileWatcher().removeObserver( fileWatcherObserverId_ );
    fileWatcherObserverId_ = 0;
    operationQueue_.shutdown();
}

void LogData::attachFile( const std::string& fileName )
{
    LOG_DEBUG << "LogData::attachFile " << fileName;

    if ( attached_file_ ) {
        // We cannot reattach
        throw CantReattachErr();
    }

    indexingFileName_ = fileName;
    attached_file_.reset( new FileHolder( keepFileClosed_ ) );
    attached_file_->open( indexingFileName_ );

    operationQueue_.enqueueOperation<AttachOperation>( fileName );
}

void LogData::interruptLoading()
{
    operationQueue_.interrupt();
}

int64_t LogData::getFileSize() const
{
    return IndexingData::ConstAccessor{ indexing_data_.get() }.getIndexedSize();
}

std::optional<std::chrono::system_clock::time_point> LogData::getLastModifiedDate() const
{
    return lastModifiedDate_;
}

// Return an initialised LogFilteredData. The search is not started.
std::unique_ptr<LogFilteredData> LogData::getNewFilteredData() const
{
    return std::make_unique<LogFilteredData>( this, postTarget_, callbacks_.onError,
                                              optionsHolder_ );
}

void LogData::reload( std::optional<klogg::Encoding> forcedEncoding )
{
    operationQueue_.interrupt();

    // Re-open the file, useful in case the file has been moved
    attached_file_->reOpenFile();

    operationQueue_.enqueueOperation<FullReindexOperation>( forcedEncoding );
}

void LogData::checkFileChanges()
{
    fileChangedOnDisk( indexingFileName_ );
}

bool LogData::hasOperationInFlight() const
{
    return operationQueue_.hasOperationInFlight();
}

void LogData::fileChangedOnDisk( const std::string& filename )
{
    LOG_INFO << "signalFileChanged " << filename << ", indexed file " << indexingFileName_;

    std::error_code fileInfoErrorCode;
    const auto fileInfoSizeOnDisk = std::filesystem::file_size(
        platform::paths::pathFromUtf8( indexingFileName_ ), fileInfoErrorCode );
    const auto fileInfoSize = fileInfoErrorCode ? -1 : static_cast<int64_t>( fileInfoSizeOnDisk );

    const auto currentFileId = FileId::getFileId( indexingFileName_ );
    const auto attachedFileId = attached_file_->getFileId();

    const auto indexedHash = IndexingData::ConstAccessor{ indexing_data_.get() }.getHash();

    LOG_INFO << "current indexed fileSize=" << indexedHash.size;
    LOG_INFO << "current indexed hash=" << indexedHash.fullDigest;
    LOG_INFO << "info file_->size()=" << fileInfoSize;

    LOG_INFO << "attached_file_->size()=" << attached_file_->size();
    LOG_INFO << "attached_file_id_ index " << attachedFileId.fileIndex;
    LOG_INFO << "currentFileId index " << currentFileId.fileIndex;

    // In absence of any clearer information, we use the following size comparison
    // to determine whether we are following the same file or not (i.e. the file
    // has been moved and the inode we are following is now under a new name, if for
    // instance log has been rotated). We want to follow the name so we have to reopen
    // the file to ensure we are reading the right one.
    // This is a crude heuristic but necessary for notification services that do not
    // give details (e.g. kqueues)

    const bool isFileIdChanged = attachedFileId != currentFileId;

    if ( !isFileIdChanged && filename != indexingFileName_ ) {
        LOG_INFO << "ignore other file update";
        return;
    }

    if ( isFileIdChanged || ( fileInfoSize != attached_file_->size() )
         || ( !attached_file_->isOpen() ) ) {

        LOG_INFO << "Inconsistent size, or file index, the file might have changed, re-opening";

        attached_file_->reOpenFile();
    }

    operationQueue_.enqueueOperation<CheckDataChangesOperation>();
}

void LogData::indexingFinished( LoadingStatus status )
{
    attached_file_->detachReader();

    LOG_INFO << "indexingFinished for: " << indexingFileName_
             << ( status == LoadingStatus::Successful ) << ", found "
             << IndexingData::ConstAccessor{ indexing_data_.get() }.getNbLines() << " lines.";

    if ( status == LoadingStatus::Successful ) {
        FileWatcher::getFileWatcher().addFile( indexingFileName_ );

        // Update the modified date/time if the file exists
        lastModifiedDate_.reset();
        std::error_code modifiedTimeErrorCode;
        const auto modifiedTime = std::filesystem::last_write_time(
            platform::paths::pathFromUtf8( indexingFileName_ ), modifiedTimeErrorCode );
        if ( !modifiedTimeErrorCode ) {
            // file_clock has no to_sys() on MSVC, so align both clocks at now().
            lastModifiedDate_ = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                modifiedTime - std::filesystem::file_time_type::clock::now()
                + std::chrono::system_clock::now() );
        }
    }

    fileChangedOnDisk_ = MonitoredFileStatus::Unchanged;

    LOG_DEBUG << "Sending indexingFinished.";
    if ( callbacks_.onLoadingFinished ) {
        callbacks_.onLoadingFinished( status );
    }
    for ( const auto& listener : loadingFinishedListeners_ ) {
        listener.second( status );
    }

    operationQueue_.finishOperationAndStartNext();
}

void LogData::checkFileChangesFinished( MonitoredFileStatus status )
{
    attached_file_->detachReader();

    LOG_INFO << "File " << indexingFileName_ << " status " << static_cast<uint8_t>( status );

    if ( fileChangedOnDisk_ != MonitoredFileStatus::Truncated ) {
        switch ( status ) {
        case MonitoredFileStatus::Truncated:
            fileChangedOnDisk_ = MonitoredFileStatus::Truncated;
            operationQueue_.enqueueOperation<FullReindexOperation>();
            break;
        case MonitoredFileStatus::DataAdded:
            fileChangedOnDisk_ = MonitoredFileStatus::DataAdded;
            operationQueue_.enqueueOperation<PartialReindexOperation>();
            break;
        case MonitoredFileStatus::Unchanged:
            fileChangedOnDisk_ = MonitoredFileStatus::Unchanged;
            break;
        }
    }
    else {
        operationQueue_.enqueueOperation<FullReindexOperation>();
    }

    if ( status != MonitoredFileStatus::Unchanged
         || fileChangedOnDisk_ == MonitoredFileStatus::Truncated ) {
        if ( callbacks_.onFileChanged ) {
            callbacks_.onFileChanged( fileChangedOnDisk_ );
        }
    }

    operationQueue_.finishOperationAndStartNext();
}

void LogData::onProgressed( int percent )
{
    if ( callbacks_.onLoadingProgressed ) {
        callbacks_.onLoadingProgressed( percent );
    }
}

size_t LogData::addLoadingFinishedListener( std::function<void( LoadingStatus )> listener )
{
    loadingFinishedListeners_.emplace( nextListenerId_, std::move( listener ) );
    return nextListenerId_++;
}

void LogData::removeLoadingFinishedListener( size_t id )
{
    loadingFinishedListeners_.erase( id );
}

//
// Implementation of virtual functions
//
LinesCount LogData::doGetNbLine() const
{
    return IndexingData::ConstAccessor{ indexing_data_.get() }.getNbLines();
}

LineLength LogData::doGetMaxLength() const
{
    return IndexingData::ConstAccessor{ indexing_data_.get() }.getMaxLength();
}

LineLength LogData::doGetLineLength( LineNumber line ) const
{
    if ( line >= IndexingData::ConstAccessor{ indexing_data_.get() }.getNbLines() ) {
        return 0_length; /* exception? */
    }

    return LineLength{ static_cast<LineLength::UnderlyingType>( doGetExpandedLineString( line ).size() ) };
}

bool LogData::doSetDisplayEncoding( const char* encoding )
{
    LOG_DEBUG << "AbstractLogData::setDisplayEncoding: " << encoding;
    codec_.setCodec( klogg::enc::fromName( encoding ) );
    auto needReload = false;
    auto useGuessedCodec = false;

    {
        IndexingData::ConstAccessor scopedAccessor{ indexing_data_.get() };

        const auto currentIndexCodec = scopedAccessor.getForcedEncoding();
        const auto effectiveIndexCodec
            = currentIndexCodec ? currentIndexCodec : scopedAccessor.getEncodingGuess();

        if ( effectiveIndexCodec && codec_.mibEnum() != effectiveIndexCodec->mib ) {
            if ( codec_.encodingParameters() != EncodingParameters( *effectiveIndexCodec ) ) {
                needReload = true;
                useGuessedCodec = scopedAccessor.getEncodingGuess()
                                  && codec_.mibEnum() == scopedAccessor.getEncodingGuess()->mib;
            }
        }
    }

    if ( needReload ) {
        reload( useGuessedCodec ? std::nullopt : codec_.codec() );
    }

    return needReload;
}

std::optional<klogg::Encoding> LogData::doGetDisplayEncoding() const
{
    return codec_.codec();
}

std::string LogData::doGetLineString( LineNumber line ) const
{
    const auto lines = doGetLines( line, 1_lcount );
    return lines.empty() ? std::string{} : lines.front();
}

std::string LogData::doGetExpandedLineString( LineNumber line ) const
{
    return untabify( doGetLineString( line ) );
}

// Note this function is also called from the LogFilteredDataWorker thread, so
// data must be protected because they are changed in the main thread (by
// indexingFinished).
klogg::vector<std::string> LogData::doGetLines( LineNumber first_line, LinesCount number ) const
{
    return getLinesFromFile( first_line, number, []( std::string&& lineData ) {
        if ( !lineData.empty() && lineData.back() == '\r' ) {
            lineData.pop_back();
        }
        return std::move( lineData );
    } );
}

klogg::vector<std::string>
LogData::doGetExpandedLines( LineNumber first_line, LinesCount number ) const
{
    return getLinesFromFile( first_line, number, []( std::string&& lineData ) {
        return untabify( std::move( lineData ) );
    } );
}

LineNumber LogData::doGetLineNumber( LineNumber index ) const
{
    return index;
}

LogData::RawLines LogData::getLinesRaw( LineNumber firstLine, LinesCount number ) const
{
    RawLines rawLines;
    rawLines.startLine = firstLine;

    try {
        IndexingData::ConstAccessor scopedAccessor{ indexing_data_.get() };
        if ( ( firstLine + number ).get() > scopedAccessor.getNbLines().get() ) {
            LOG_WARNING << "Lines out of bound asked for";
            return {}; /* exception? */
        }

        rawLines.endOfLines.reserve( number.get() );

        ScopedFileHolder<FileHolder> fileHolder( attached_file_.get() );

        const auto firstByte
            = ( firstLine == 0_lnum )
                  ? 0
                  : scopedAccessor.getEndOfLineOffset( firstLine - 1_lcount ).get();

        klogg::vector<OffsetInFile> endOfLines
            = scopedAccessor.getEndOfLineOffsets( firstLine, number );

        const auto lastByte = endOfLines.back().get();

        std::transform(
            endOfLines.begin(), endOfLines.end(), std::back_inserter( rawLines.endOfLines ),
            [ firstByte ]( const OffsetInFile& offset ) { return offset.get() - firstByte; } );

        const auto bytesToRead = lastByte - firstByte;
        LOG_DEBUG << "will try to read:" << bytesToRead << " bytes";
        rawLines.buffer.resize( static_cast<std::size_t>( bytesToRead ) );

        const auto bytesRead
            = fileHolder.getFile()->readAt( static_cast<uint64_t>( firstByte ),
                                            rawLines.buffer.data(),
                                            static_cast<size_t>( bytesToRead ) );

        if ( bytesRead != static_cast<size_t>( bytesToRead ) ) {
            LOG_DEBUG << "failed to read " << bytesToRead << " bytes, got " << bytesRead;
        }

        LOG_DEBUG << "done reading lines:" << rawLines.buffer.size();
        rawLines.textDecoder = codec_.makeDecoder();
        return rawLines;

    } catch ( const std::bad_alloc& ) {
        LOG_ERROR << "not enough memory";
        rawLines.endOfLines.clear();
        rawLines.buffer.clear();
        return rawLines;
    }
}

klogg::vector<std::string>
LogData::getLinesFromFile( LineNumber firstLine, LinesCount number,
                           std::string ( *processLine )( std::string&& ) ) const
{
    LOG_DEBUG << "firstLine:" << firstLine << " nb:" << number;

    if ( number.get() == 0 ) {
        return klogg::vector<std::string>();
    }

    klogg::vector<std::string> processedLines;
    try {
        const auto rawLines = getLinesRaw( firstLine, number );
        auto decodedLines = rawLines.decodeLines();

        processedLines.reserve( decodedLines.size() );

        for ( auto&& line : decodedLines ) {
            processedLines.push_back( processLine( std::move( line ) ) );
        }

    } catch ( const std::bad_alloc& e ) {
        LOG_ERROR << "not enough memory " << e.what();
        processedLines.emplace_back( "KLOGG WARNING: not enough memory" );
    }

    processedLines.reserve( number.get() - processedLines.size() );
    while ( processedLines.size() < number.get() ) {
        processedLines.emplace_back( "KLOGG WARNING: failed to read some lines before this one" );
    }

    return processedLines;
}

klogg::vector<std::string>
LogData::getLinesForLineNumbers( const klogg::vector<LineNumber>& lines ) const
{
    klogg::vector<std::string> decodedLines;
    if ( lines.empty() ) {
        return decodedLines;
    }

    decodedLines.reserve( lines.size() );
    try {
        IndexingData::ConstAccessor scopedAccessor{ indexing_data_.get() };
        ScopedFileHolder<FileHolder> fileHolder( attached_file_.get() );

        const auto nbLines = scopedAccessor.getNbLines();
        auto textDecoder = codec_.makeDecoder();
        const auto lineFeedWidth = textDecoder.encodingParams.lineFeedWidth;

        klogg::vector<char> buffer;
        for ( const auto line : lines ) {
            // The contiguous path pads out-of-bound lines with a warning
            if ( line >= nbLines ) {
                LOG_WARNING << "Lines out of bound asked for";
                decodedLines.emplace_back(
                    "KLOGG WARNING: failed to read some lines before this one" );
                continue;
            }

            // End-of-line offsets point past the line feed
            const auto firstByte = ( line == 0_lnum )
                                       ? 0
                                       : scopedAccessor.getEndOfLineOffset( line - 1_lcount ).get();
            const auto lastByte = scopedAccessor.getEndOfLineOffset( line ).get();
            const auto length = lastByte - firstByte - lineFeedWidth;

            constexpr auto maxlength = std::numeric_limits<int>::max() / 2;
            if ( length >= maxlength ) {
                decodedLines.emplace_back( "KLOGG WARNING: this line is too long" );
                continue;
            }

            buffer.resize( static_cast<size_t>( length ) );
            const auto bytesRead
                = fileHolder.getFile()->readAt( static_cast<uint64_t>( firstByte ), buffer.data(),
                                                static_cast<size_t>( length ) );
            if ( bytesRead != static_cast<size_t>( length ) ) {
                LOG_WARNING << "not enough data in buffer";
                decodedLines.emplace_back( "KLOGG WARNING: file read failed" );
                continue;
            }

            auto decodedLine
                = textDecoder.decodeBlock( buffer.data(), type_safe::narrow_cast<int>( length ) );
            if ( !decodedLine.empty() && decodedLine.back() == '\r' ) {
                decodedLine.pop_back();
            }
            decodedLines.push_back( std::move( decodedLine ) );
        }
    } catch ( const std::bad_alloc& ) {
        LOG_ERROR << "not enough memory";
        // One entry per requested line is the batch contract (same padding as
        // getLinesFromFile): fill the gap the failure left instead of pushing
        // one entry past the request, which would shift the caller's line
        // numbers and index its text array out of bounds
        try {
            decodedLines.resize( lines.size(), "KLOGG WARNING: not enough memory" );
        } catch ( const std::bad_alloc& ) {
            LOG_ERROR << "not enough memory to pad the line batch";
        }
    }

    return decodedLines;
}

std::optional<klogg::Encoding> LogData::getDetectedEncoding() const
{
    return IndexingData::ConstAccessor{ indexing_data_.get() }.getEncodingGuess();
}

std::optional<klogg::Encoding> LogData::getForcedEncoding() const
{
    return IndexingData::ConstAccessor{ indexing_data_.get() }.getForcedEncoding();
}

void LogData::doAttachReader() const
{
    attached_file_->attachReader();
}

void LogData::doDetachReader() const
{
    attached_file_->detachReader();
}

klogg::vector<std::string> LogData::RawLines::decodeLines() const
{
    if ( this->endOfLines.empty() ) {
        return klogg::vector<std::string>();
    }

    klogg::vector<std::string> decodedLines;
    decodedLines.reserve( this->endOfLines.size() );

    try {
        int64_t lineStart = 0;
        size_t currentLineIndex = 0;
        const auto lineFeedWidth = textDecoder.encodingParams.lineFeedWidth;
        for ( const auto& lineEnd : this->endOfLines ) {
            const auto length = lineEnd - lineStart - lineFeedWidth;
            LOG_DEBUG << "line " << this->startLine.get() + currentLineIndex << ", length "
                      << length;

            constexpr auto maxlength = std::numeric_limits<int>::max() / 2;
            if ( length >= maxlength ) {
                decodedLines.emplace_back( "KLOGG WARNING: this line is too long" );
                break;
            }

            if ( lineStart + length > klogg::ssize( buffer ) ) {
                decodedLines.emplace_back( "KLOGG WARNING: file read failed" );
                LOG_WARNING << "not enough data in buffer";
                break;
            }

            auto decodedLine = textDecoder.decodeBlock( buffer.data() + lineStart,
                                                        type_safe::narrow_cast<int>( length ) );

            decodedLines.push_back( std::move( decodedLine ) );

            lineStart = lineEnd;
        }
    } catch ( const std::bad_alloc& ) {
        LOG_ERROR << "not enough memory";
        decodedLines.emplace_back( "KLOGG WARNING: not enough memory" );
    }

    decodedLines.reserve( this->endOfLines.size() - decodedLines.size() );
    while ( decodedLines.size() < this->endOfLines.size() ) {
        decodedLines.emplace_back( "KLOGG WARNING: failed to decode some lines before this one" );
    }

    return decodedLines;
}

klogg::vector<std::string_view> LogData::RawLines::buildUtf8View() const
{
    klogg::vector<std::string_view> lines;
    if ( this->endOfLines.empty() || textDecoder.decoder == nullptr ) {
        return lines;
    }

    try {
        lines.reserve( endOfLines.size() );

        std::string_view wholeString;

        if ( textDecoder.encodingParams.isUtf8Compatible ) {
            wholeString = std::string_view( buffer.data(), buffer.size() );
        }
        else {
            std::string decodedUtf8
                = textDecoder.decodeBlock( buffer.data(), klogg::isize( buffer ) );

            utf8Data_.resize( decodedUtf8.size() );
            std::copy( decodedUtf8.begin(), decodedUtf8.end(), utf8Data_.begin() );

            wholeString = std::string_view( utf8Data_.data(), utf8Data_.size() );
        }

        auto nextLineFeed = wholeString.find( '\n' );
        while ( nextLineFeed != std::string_view::npos ) {
            lines.push_back( wholeString.substr( 0, nextLineFeed ) );
            wholeString.remove_prefix( nextLineFeed + 1 );
            nextLineFeed = wholeString.find( '\n' );
        }

        if ( !wholeString.empty() ) {
            lines.push_back( wholeString );
        }

    } catch ( const std::exception& e ) {
        LOG_ERROR << "failed to transform lines to utf8 " << e.what();
        const auto lastLineOffset = utf8Data_.size();
        // utf8Data_.append( "KLOGG WARNING: not enough memory, try decrease search buffer" );
        lines.reserve( this->endOfLines.size() - lines.size() );
        while ( lines.size() < this->endOfLines.size() ) {
            lines.emplace_back( utf8Data_.data() + lastLineOffset,
                                utf8Data_.size() - lastLineOffset );
        }
    }

    return lines;
}
