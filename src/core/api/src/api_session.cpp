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

#include "klogg/api_session.h"

#include <limits>
#include <utility>

#include "encoding.h"
#include "encodings.h"
#include "eventvisualization.h"
#include "klogg_version.h"
#include "regularexpression.h"

#include "session_impl.h"
#include "tracing.h"

namespace klogg::api {

namespace {

// Highlight mutations are what the paint path sees through the published
// snapshot; a successful one republishes it on the worker, a failed one left
// the state alone, so the snapshot would be identical.
template <typename R>
R republishPaintHighlightsOnSuccess( EngineContext* ctx, R result )
{
    if ( result ) {
        ctx->publishPaintHighlights();
    }
    return result;
}

} // namespace

double scrollMultiplierForLines( LinesCount lines )
{
    constexpr auto intMax
        = static_cast<LinesCount::UnderlyingType>( std::numeric_limits<int>::max() );
    if ( lines.get() <= intMax ) {
        return 1.0;
    }
    return static_cast<double>( std::numeric_limits<int>::max() )
           / static_cast<double>( lines.get() );
}

Session::Session( EngineOptions options )
    : impl_( std::make_unique<Impl>( std::move( options ) ) )
{
}

Session::~Session() = default;

Result<std::unique_ptr<Session>> Session::create( const EngineOptions& options )
{
    // First call in every consumer process; no-op unless KLOGG_PERFETTO_TRACE
    // points at the trace output file
    tracing::initializeFromEnvironment();

    // Platform services contract: the composition root has already created its
    // application object and brought up the persistent stores by the time a
    // session is created �?the klogg app does it in main() (QApplication +
    // Configuration::getSynced()) before calling this, klogg_grep initializes
    // the stores on its own, test mains do the same. Both halves are load
    // bearing: platform::Settings throws when nothing initialized the stores
    // (persistable.h), and Configuration::get() throws until its persistable
    // was initialized once (the shortcut state reads through it). A
    // missing initialization is a caller bug this function does not paper
    // over �?the engine creates no application object and no store of its own.
    return std::unique_ptr<Session>( new Session( options ) );
}

Result<void> Session::updateOptions( const EngineOptions& options )
{
    KLOGG_TRACE_SCOPE( "api", "Session::updateOptions" );
    return impl_->dispatch( [ &options ]( EngineContext* ctx ) -> Result<void> {
        ctx->optionsHolder->set( options );
        return {};
    } );
}

Result<FileInfo> Session::openFileSync( const std::string& path,
                                        const std::optional<std::string>& encoding,
                                        bool allowMissing )
{
    KLOGG_TRACE_SCOPE( "api", "Session::openFileSync" );
    const auto id = impl_->nextFileId++;
    return impl_->dispatch( [ & ]( EngineContext* ctx ) {
        return ctx->openFileOnWorker( id, path, encoding, allowMissing );
    } );
}

Result<FileInfo> Session::openFileAsync( const std::string& path, FileCallback onDone,
                                         const std::optional<std::string>& encoding,
                                         bool allowMissing )
{
    KLOGG_TRACE_SCOPE( "api", "Session::openFileAsync" );
    const auto id = impl_->nextFileId++;
    // Create the entry synchronously: the file is attached and indexing has
    // started when this returns, which makes same-path dedup atomic and the
    // FileId usable immediately
    return impl_->dispatch( [ &, onDone = std::move( onDone ) ]( EngineContext* ctx ) mutable {
        return ctx->openFileAsyncOnWorker( id, path, encoding, std::move( onDone ), allowMissing );
    } );
}

Result<void> Session::closeFile( FileId id )
{
    KLOGG_TRACE_SCOPE( "api", "Session::closeFile" );
    return impl_->dispatch( [ id ]( EngineContext* ctx ) { return ctx->closeFileOnWorker( id ); } );
}

Result<FileInfo> Session::reloadFile( FileId id )
{
    KLOGG_TRACE_SCOPE( "api", "Session::reloadFile" );
    return impl_->dispatch(
        [ id ]( EngineContext* ctx ) { return ctx->reloadFileOnWorker( id ); } );
}

Result<void> Session::reloadFileAsync( FileId id, FileCallback onDone )
{
    KLOGG_TRACE_SCOPE( "api", "Session::reloadFileAsync" );
    return impl_->dispatch( [ id, onDone = std::move( onDone ) ]( EngineContext* ctx ) mutable {
        return ctx->reloadFileAsyncOnWorker( id, std::move( onDone ) );
    } );
}

Result<void> Session::interruptLoading( FileId id )
{
    KLOGG_TRACE_SCOPE( "api", "Session::interruptLoading" );
    return impl_->dispatch(
        [ id ]( EngineContext* ctx ) { return ctx->interruptLoadingOnWorker( id ); } );
}

Result<FileInfo> Session::fileInfo( FileId id ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::fileInfo" );
        return impl_->dispatch( [ id ]( EngineContext* ctx ) { return ctx->fileInfo( id ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::fileInfo" );
    return impl_->context->fileInfo( id );
}

Result<std::vector<FileInfo>> Session::openFiles() const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::openFiles" );
        return impl_->dispatch( []( EngineContext* ctx ) { return ctx->openFiles(); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::openFiles" );
    return impl_->context->openFiles();
}

Result<void> Session::watchFile( FileId id, bool enabled )
{
    KLOGG_TRACE_SCOPE( "api", "Session::watchFile" );
    return impl_->dispatch(
        [ id, enabled ]( EngineContext* ctx ) { return ctx->watchFileOnWorker( id, enabled ); } );
}

Result<void> Session::checkFileChanges( FileId id )
{
    KLOGG_TRACE_SCOPE( "api", "Session::checkFileChanges" );
    return impl_->dispatch(
        [ id ]( EngineContext* ctx ) { return ctx->checkFileChangesOnWorker( id ); } );
}

Result<void> Session::extendSearch( SearchId id, LineNumber toLine )
{
    KLOGG_TRACE_SCOPE( "api", "Session::extendSearch" );
    return impl_->dispatch( [ id, toLine ]( EngineContext* ctx ) {
        return ctx->extendSearchOnWorker( id, toLine );
    } );
}

Result<void> Session::setEncoding( FileId id, const std::string& encoding )
{
    KLOGG_TRACE_SCOPE( "api", "Session::setEncoding" );
    return impl_->dispatch(
        [ & ]( EngineContext* ctx ) { return ctx->setEncodingOnWorker( id, encoding ); } );
}

Result<void> Session::setEncodingAsync( FileId id, const std::string& encoding,
                                        FileCallback onDone )
{
    KLOGG_TRACE_SCOPE( "api", "Session::setEncodingAsync" );
    return impl_->dispatch(
        [ id, &encoding, onDone = std::move( onDone ) ]( EngineContext* ctx ) mutable {
            return ctx->setEncodingAsyncOnWorker( id, encoding, std::move( onDone ) );
        } );
}

Result<std::vector<std::string>> Session::supportedEncodings() const
{
    // Pure static data plus read-only registry lookups: no worker round-trip
    std::vector<std::string> names;
    for ( const auto& [ group, mibs ] : Encodings::supportedEncodings() ) {
        for ( const auto mib : mibs ) {
            if ( const auto encoding = klogg::enc::fromMib( mib ) ) {
                names.emplace_back( encoding->name );
            }
        }
    }
    return names;
}

Result<std::vector<LineData>> Session::getLines( FileId id, LineNumber from, LinesCount count,
                                                 AnsiMode ansi )
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::getLines" );
        return impl_->dispatch(
            [ & ]( EngineContext* ctx ) { return ctx->getLines( id, from, count, ansi ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::getLines" );
    return impl_->context->getLines( id, from, count, ansi );
}

Result<std::vector<LineData>>
Session::getLinesBatch( FileId id, const std::vector<LineNumber>& lines, AnsiMode ansi )
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::getLinesBatch" );
        return impl_->dispatch(
            [ & ]( EngineContext* ctx ) { return ctx->getLinesBatch( id, lines, ansi ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::getLinesBatch" );
    return impl_->context->getLinesBatch( id, lines, ansi );
}

Result<PatternInfo> Session::validatePattern( const std::string& pattern, bool isRegex ) const
{
    // A pattern that carries no regex metacharacters behaves as plain text
    constexpr const char* regexMetachars = "\\^$.|?*+()[]{}";

    PatternInfo info;
    info.isPlainText = !isRegex || pattern.find_first_of( regexMetachars ) == std::string::npos;

    const RegularExpressionPattern enginePattern( pattern, false /* caseSensitive */,
                                                  false /* inverse */, false /* boolean */,
                                                  !isRegex /* plainText */ );
    const RegularExpression expression( enginePattern );
    info.valid = expression.isValid();
    if ( !info.valid ) {
        info.error = expression.errorString();
    }
    return info;
}

Result<SearchId> Session::runSearch( FileId id, const SearchParams& params )
{
    KLOGG_TRACE_SCOPE( "api", "Session::runSearch" );
    const auto searchId = impl_->nextSearchId++;
    return impl_->dispatch(
        [ & ]( EngineContext* ctx ) { return ctx->runSearchOnWorker( id, searchId, params ); } );
}

Result<SearchId> Session::runBooleanSearch( FileId id, const std::string& expression )
{
    KLOGG_TRACE_SCOPE( "api", "Session::runBooleanSearch" );
    const auto searchId = impl_->nextSearchId++;
    SearchParams params;
    params.pattern = expression;
    params.isBoolean = true;
    return impl_->dispatch(
        [ & ]( EngineContext* ctx ) { return ctx->runSearchOnWorker( id, searchId, params ); } );
}

Result<SearchResult> Session::searchProgress( SearchId id ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::searchProgress" );
        return impl_->dispatch(
            [ id ]( EngineContext* ctx ) { return ctx->searchProgress( id ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::searchProgress" );
    return impl_->context->searchProgress( id );
}

Result<std::vector<LineNumber>> Session::searchMatches( SearchId id, uint64_t from, uint32_t count )
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::searchMatches" );
        return impl_->dispatch(
            [ & ]( EngineContext* ctx ) { return ctx->searchMatches( id, from, count ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::searchMatches" );
    return impl_->context->searchMatches( id, from, count );
}

Result<std::optional<uint64_t>> Session::searchMatchIndex( SearchId id, LineNumber line ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::searchMatchIndex" );
        return impl_->dispatch(
            [ id, line ]( EngineContext* ctx ) { return ctx->searchMatchIndex( id, line ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::searchMatchIndex" );
    return impl_->context->searchMatchIndex( id, line );
}

Result<std::vector<LineData>> Session::getFilteredLines( SearchId id, uint64_t fromIndex,
                                                         uint32_t count, AnsiMode ansi )
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::getFilteredLines" );
        return impl_->dispatch( [ & ]( EngineContext* ctx ) {
            return ctx->getFilteredLines( id, fromIndex, count, ansi );
        } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::getFilteredLines" );
    return impl_->context->getFilteredLines( id, fromIndex, count, ansi );
}

Result<std::vector<LineNumber>> Session::filteredLineNumbers( SearchId id, uint64_t fromIndex,
                                                              uint32_t count )
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::filteredLineNumbers" );
        return impl_->dispatch( [ & ]( EngineContext* ctx ) {
            return ctx->filteredLineNumbers( id, fromIndex, count );
        } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::filteredLineNumbers" );
    return impl_->context->filteredLineNumbers( id, fromIndex, count );
}

Result<LinesCount> Session::setSearchVisibility( SearchId id, bool matches, bool marks,
                                                 bool events )
{
    KLOGG_TRACE_SCOPE( "api", "Session::setSearchVisibility" );
    return impl_->dispatch( [ & ]( EngineContext* ctx ) {
        return ctx->setSearchVisibilityOnWorker( id, matches, marks, events );
    } );
}

Result<LinesCount> Session::getFilteredLineCount( SearchId id ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::getFilteredLineCount" );
        return impl_->dispatch(
            [ id ]( EngineContext* ctx ) { return ctx->getFilteredLineCount( id ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::getFilteredLineCount" );
    return impl_->context->getFilteredLineCount( id );
}

Result<std::optional<uint64_t>> Session::filteredLineIndex( SearchId id, LineNumber line ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::filteredLineIndex" );
        return impl_->dispatch(
            [ id, line ]( EngineContext* ctx ) { return ctx->filteredLineIndex( id, line ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::filteredLineIndex" );
    return impl_->context->filteredLineIndex( id, line );
}

Result<std::optional<uint64_t>> Session::filteredLineIndexAtOrBefore( SearchId id,
                                                                      LineNumber line ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::filteredLineIndexAtOrBefore" );
        return impl_->dispatch( [ id, line ]( EngineContext* ctx ) {
            return ctx->filteredLineIndexAtOrBefore( id, line );
        } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::filteredLineIndexAtOrBefore" );
    return impl_->context->filteredLineIndexAtOrBefore( id, line );
}

Result<LineLength> Session::filteredMaxLineLength( SearchId id ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::filteredMaxLineLength" );
        return impl_->dispatch(
            [ id ]( EngineContext* ctx ) { return ctx->filteredMaxLineLength( id ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::filteredMaxLineLength" );
    return impl_->context->filteredMaxLineLength( id );
}

Result<void> Session::cancelSearch( SearchId id )
{
    KLOGG_TRACE_SCOPE( "api", "Session::cancelSearch" );
    return impl_->dispatch(
        [ id ]( EngineContext* ctx ) { return ctx->cancelSearchOnWorker( id ); } );
}

Result<void> Session::interruptSearch( SearchId id )
{
    KLOGG_TRACE_SCOPE( "api", "Session::interruptSearch" );
    return impl_->dispatch(
        [ id ]( EngineContext* ctx ) { return ctx->interruptSearchOnWorker( id ); } );
}

void Session::onSearchProgress( SearchId id, ProgressCallback cb )
{
    KLOGG_TRACE_SCOPE( "api", "Session::onSearchProgress" );
    impl_->dispatch(
        [ & ]( EngineContext* ctx ) { ctx->addProgressCallbackOnWorker( id, std::move( cb ) ); } );
}

void Session::subscribe( EventHandler handler )
{
    std::lock_guard lock( impl_->handlersMutex );
    impl_->handlers.push_back( std::move( handler ) );
}

std::string Session::version() const
{
    return std::string( kloggAppVersion() );
}

// ====== Shared state API ======

Result<void> Session::setHighlighters( const std::vector<HighlightRule>& rules )
{
    KLOGG_TRACE_SCOPE( "api", "Session::setHighlighters" );
    return impl_->dispatch( [ & ]( EngineContext* ctx ) {
        return republishPaintHighlightsOnSuccess( ctx, ctx->setHighlighters( rules ) );
    } );
}

Result<void> Session::setQuickHighlighters( const std::vector<QuickHighlighterDef>& defs )
{
    KLOGG_TRACE_SCOPE( "api", "Session::setQuickHighlighters" );
    return impl_->dispatch( [ & ]( EngineContext* ctx ) {
        return republishPaintHighlightsOnSuccess( ctx, ctx->setQuickHighlighters( defs ) );
    } );
}

Result<std::vector<LineNumber>> Session::listMarks( FileId id ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::listMarks" );
        return impl_->dispatch( [ id ]( EngineContext* ctx ) { return ctx->listMarks( id ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::listMarks" );
    return impl_->context->listMarks( id );
}

Result<void> Session::addMarks( FileId id, const std::vector<LineNumber>& lines )
{
    KLOGG_TRACE_SCOPE( "api", "Session::addMarks" );
    return impl_->dispatch(
        [ & ]( EngineContext* ctx ) { return ctx->addMarksOnWorker( id, lines ); } );
}

Result<void> Session::removeMarks( FileId id, const std::vector<LineNumber>& lines )
{
    KLOGG_TRACE_SCOPE( "api", "Session::removeMarks" );
    return impl_->dispatch(
        [ & ]( EngineContext* ctx ) { return ctx->removeMarksOnWorker( id, lines ); } );
}

Result<std::optional<LineNumber>> Session::markAfter( FileId id, LineNumber line ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::markAfter" );
        return impl_->dispatch(
            [ id, line ]( EngineContext* ctx ) { return ctx->markAfter( id, line ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::markAfter" );
    return impl_->context->markAfter( id, line );
}

Result<std::optional<LineNumber>> Session::markBefore( FileId id, LineNumber line ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::markBefore" );
        return impl_->dispatch(
            [ id, line ]( EngineContext* ctx ) { return ctx->markBefore( id, line ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::markBefore" );
    return impl_->context->markBefore( id, line );
}

Result<std::vector<uint8_t>> Session::lineFlags( FileId id, std::optional<SearchId> searchId,
                                                 LineNumber from, LinesCount count ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::lineFlags" );
        return impl_->dispatch(
            [ & ]( EngineContext* ctx ) { return ctx->lineFlags( id, searchId, from, count ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::lineFlags" );
    return impl_->context->lineFlags( id, searchId, from, count );
}

Result<std::vector<uint8_t>> Session::lineFlagsBatch( FileId id, std::optional<SearchId> searchId,
                                                      const std::vector<LineNumber>& lines ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::lineFlagsBatch" );
        return impl_->dispatch(
            [ & ]( EngineContext* ctx ) { return ctx->lineFlagsBatch( id, searchId, lines ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::lineFlagsBatch" );
    return impl_->context->lineFlagsBatch( id, searchId, lines );
}

Result<double> Session::scrollMultiplier( FileId id ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::scrollMultiplier" );
        return impl_->dispatch(
            [ id ]( EngineContext* ctx ) { return ctx->scrollMultiplier( id ); } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::scrollMultiplier" );
    return impl_->context->scrollMultiplier( id );
}

Result<LineNumber> Session::scrollPosToLine( FileId id, int64_t scrollPos ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::scrollPosToLine" );
        return impl_->dispatch( [ id, scrollPos ]( EngineContext* ctx ) {
            return ctx->scrollPosToLine( id, scrollPos );
        } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::scrollPosToLine" );
    return impl_->context->scrollPosToLine( id, scrollPos );
}

Result<int64_t> Session::lineToScrollPos( FileId id, LineNumber line ) const
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::lineToScrollPos" );
        return impl_->dispatch( [ id, line ]( EngineContext* ctx ) {
            return ctx->lineToScrollPos( id, line );
        } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::lineToScrollPos" );
    return impl_->context->lineToScrollPos( id, line );
}

Result<ViewportResult> Session::renderViewport( FileId id, std::optional<SearchId> searchId,
                                                LineNumber firstLine, LinesCount visibleLines,
                                                const ViewportOptions& options )
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::renderViewport" );
        return impl_->dispatch( [ & ]( EngineContext* ctx ) {
            return ctx->renderViewport( id, searchId, firstLine, visibleLines, options );
        } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::renderViewport" );
    return impl_->context->renderViewport( id, searchId, firstLine, visibleLines, options );
}

Result<RenderedHtml> Session::renderLinesHtml( FileId id, std::optional<SearchId> searchId,
                                               const std::vector<LineNumber>& lines,
                                               const HtmlRenderOptions& options )
{
    if ( readChannelQueued() ) {
        KLOGG_TRACE_SCOPE( "api", "Session::renderLinesHtml" );
        return impl_->dispatch( [ & ]( EngineContext* ctx ) {
            return ctx->renderLinesHtml( id, searchId, lines, options );
        } );
    }
    KLOGG_TRACE_SCOPE( "api.read", "Session::renderLinesHtml" );
    return impl_->context->renderLinesHtml( id, searchId, lines, options );
}

Result<std::string> Session::saveSession() const
{
    KLOGG_TRACE_SCOPE( "api", "Session::saveSession" );
    return impl_->dispatch( []( EngineContext* ctx ) { return ctx->saveSessionOnWorker(); } );
}

Result<void> Session::restoreSession( const std::string& json )
{
    KLOGG_TRACE_SCOPE( "api", "Session::restoreSession" );
    auto files = impl_->dispatch( [ & ]( EngineContext* ctx ) {
        // The restore replaces the shared state, highlights included
        return republishPaintHighlightsOnSuccess( ctx, ctx->restoreSession( json ) );
    } );
    if ( !files ) {
        return tl::make_unexpected( files.error() );
    }

    // Reopen files one by one; a single failure must not stop the rest
    std::vector<std::string> failedPaths;
    for ( const auto& file : *files ) {
        const auto id = impl_->nextFileId++;
        std::optional<std::string> encoding;
        if ( !file.encoding.empty() ) {
            encoding = file.encoding;
        }

        auto opened = impl_->dispatch( [ & ]( EngineContext* ctx ) {
            return ctx->openFileOnWorker( id, file.path, encoding, false /* allowMissing */ );
        } );
        if ( !opened ) {
            failedPaths.push_back( file.path );
            continue;
        }

        if ( !file.marks.empty() ) {
            std::vector<LineNumber> marks;
            marks.reserve( file.marks.size() );
            for ( const auto mark : file.marks ) {
                marks.push_back( LineNumber( mark ) );
            }
            // opened->id, not the pre-allocated id: a same-path entry in the
            // restored list shares the first open's FileId
            impl_->dispatch( [ & ]( EngineContext* ctx ) {
                return ctx->addMarksOnWorker( opened->id, marks );
            } );
        }
    }

    if ( !failedPaths.empty() ) {
        std::string message = "failed to restore files:";
        for ( const auto& path : failedPaths ) {
            message += " " + path;
        }
        return tl::make_unexpected( Error{ ErrorCode::InternalError, message } );
    }
    return {};
}

// ====== Event visualization API (log -> structured events -> Perfetto) ======

Result<std::vector<LogTypeConfig>> Session::listLogTypeConfigs() const
{
    KLOGG_TRACE_SCOPE( "api", "Session::listLogTypeConfigs" );
    return impl_->dispatch(
        []( EngineContext* ctx ) { return ctx->listLogTypeConfigs(); } );
}

Result<void> Session::replaceLogTypeConfigs( const std::vector<LogTypeConfig>& configs )
{
    KLOGG_TRACE_SCOPE( "api", "Session::replaceLogTypeConfigs" );
    return impl_->dispatch(
        [ & ]( EngineContext* ctx ) { return ctx->replaceLogTypeConfigs( configs ); } );
}

Result<ExtractionId> Session::extractEvents( FileId id, const ExtractionParams& params )
{
    KLOGG_TRACE_SCOPE( "api", "Session::extractEvents" );
    const auto extractionId = impl_->nextExtractionId++;
    return impl_->dispatch( [ & ]( EngineContext* ctx ) {
        return ctx->extractEventsOnWorker( id, extractionId, params );
    } );
}

Result<ExtractionProgress> Session::extractionProgress( ExtractionId id ) const
{
    KLOGG_TRACE_SCOPE( "api", "Session::extractionProgress" );
    return impl_->dispatch(
        [ id ]( EngineContext* ctx ) { return ctx->extractionProgressOnWorker( id ); } );
}

Result<std::vector<ExtractedEvent>> Session::extractedEvents( ExtractionId id, uint64_t from,
                                                              uint32_t count )
{
    KLOGG_TRACE_SCOPE( "api", "Session::extractedEvents" );
    return impl_->dispatch( [ id, from, count ]( EngineContext* ctx ) {
        return ctx->extractedEventsOnWorker( id, from, count );
    } );
}

Result<void> Session::cancelExtraction( ExtractionId id )
{
    KLOGG_TRACE_SCOPE( "api", "Session::cancelExtraction" );
    return impl_->dispatch(
        [ id ]( EngineContext* ctx ) { return ctx->cancelExtractionOnWorker( id ); } );
}

Result<std::vector<SliceRange>> Session::sliceRanges( ExtractionId id ) const
{
    KLOGG_TRACE_SCOPE( "api", "Session::sliceRanges" );
    return impl_->dispatch(
        [ id ]( EngineContext* ctx ) { return ctx->sliceRangesOnWorker( id ); } );
}

Result<void> Session::setEventCategoryFilter( FileId id, std::vector<std::string> categories )
{
    KLOGG_TRACE_SCOPE( "api", "Session::setEventCategoryFilter" );
    return impl_->dispatch( [ & ]( EngineContext* ctx ) {
        return ctx->setEventCategoryFilterOnWorker( id, std::move( categories ) );
    } );
}

Result<EventExportResult>
Session::exportEvents( ExtractionId id, const std::string& outputPath, ExportFormat format,
                       std::optional<std::vector<std::string>> categories )
{
    KLOGG_TRACE_SCOPE( "api", "Session::exportEvents" );
    return impl_->dispatch(
        [ id, &outputPath, format, categories = std::move( categories ) ]( EngineContext* ctx ) {
            return ctx->exportEventsOnWorker( id, outputPath, format, categories );
        } );
}

Result<std::string> Session::generateEventPattern( const std::string& selectedText ) const
{
    // Pure string transformation: no worker round-trip
    return EventVisualizationManager::generateRegexFromText( selectedText );
}

} // namespace klogg::api
