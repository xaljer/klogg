/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

#include <catch2/catch.hpp>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QThread>
#include <QUuid>

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "klogg/api.h"

using namespace klogg::api;

namespace {

constexpr int kWaitTimeoutMs = 20000;

struct TempLogFile {
    QString path;

    TempLogFile() = default;
    TempLogFile( const TempLogFile& ) = delete;
    TempLogFile& operator=( const TempLogFile& ) = delete;
    TempLogFile( TempLogFile&& ) = default;
    TempLogFile& operator=( TempLogFile&& ) = default;

    ~TempLogFile()
    {
        if ( !path.isEmpty() ) {
            QFile::remove( path );
        }
    }
};

TempLogFile writeTempLogFile( const QStringList& lines )
{
    TempLogFile file;
    file.path = QString( "%1/klogg_api_test_%2.log" )
                    .arg( QDir::tempPath(), QUuid::createUuid().toString( QUuid::WithoutBraces ) );

    QFile out( file.path );
    REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Text ) );
    QTextStream stream( &out );
    for ( const auto& line : lines ) {
        stream << line << "\n";
    }
    out.close();
    return file;
}

std::unique_ptr<Session> createSession()
{
    auto session = Session::create();
    REQUIRE( session.has_value() );
    return std::move( session.value() );
}

FileInfo openSyncOrFail( Session& session, const QString& path )
{
    auto info = session.openFileSync( path.toStdString() );
    REQUIRE( info.has_value() );
    return *info;
}

bool waitForSearchDone( Session& session, SearchId id )
{
    QElapsedTimer timer;
    timer.start();
    while ( timer.elapsed() < kWaitTimeoutMs ) {
        const auto progress = session.searchProgress( id );
        if ( !progress ) {
            return false;
        }
        if ( progress->progress >= 1.0f ) {
            return true;
        }
        QThread::msleep( 10 );
    }
    return false;
}

SearchId runSearchOrFail( Session& session, FileId id, const std::string& pattern )
{
    SearchParams params;
    params.pattern = pattern;
    auto search = session.runSearch( id, params );
    REQUIRE( search.has_value() );
    REQUIRE( waitForSearchDone( session, *search ) );
    return *search;
}

} // namespace

TEST_CASE( "API setSearchVisibility unions session marks into the filtered view",
           "[api][projection]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile(
        { "INFO l0", "WARN l1", "INFO l2", "WARN l3", "INFO l4", "INFO l5", "INFO l6" } );
    const auto info = openSyncOrFail( *session, file.path );

    const auto search = runSearchOrFail( *session, info.id, "WARN" );

    // A fresh search shows matches only
    const auto initialCount = session->getFilteredLineCount( search );
    REQUIRE( initialCount.has_value() );
    REQUIRE( *initialCount == 2_lcount );

    // Line 1 is both a match and a mark: the union must not double-count it
    REQUIRE( session->addMarks( info.id, { 0_lnum, 1_lnum, 5_lnum } ).has_value() );

    const auto unionCount = session->setSearchVisibility( search, true, true, false );
    REQUIRE( unionCount.has_value() );
    REQUIRE( *unionCount == 4_lcount );

    const auto countAfter = session->getFilteredLineCount( search );
    REQUIRE( countAfter.has_value() );
    REQUIRE( *countAfter == 4_lcount );

    const auto unionLines = session->getFilteredLines( search, 0, 10 );
    REQUIRE( unionLines.has_value() );
    REQUIRE( unionLines->size() == 4 );
    REQUIRE( unionLines->at( 0 ).num == 0_lnum );
    REQUIRE( unionLines->at( 1 ).num == 1_lnum );
    REQUIRE( unionLines->at( 2 ).num == 3_lnum );
    REQUIRE( unionLines->at( 3 ).num == 5_lnum );

    // Events join the union only when an extraction has populated the file's
    // event lines; no extraction has run here, so the count stays unchanged
    const auto withEvents = session->setSearchVisibility( search, true, true, true );
    REQUIRE( withEvents.has_value() );
    REQUIRE( *withEvents == 4_lcount );

    // Marks only
    const auto marksCount = session->setSearchVisibility( search, false, true, false );
    REQUIRE( marksCount.has_value() );
    REQUIRE( *marksCount == 3_lcount );

    const auto markLines = session->getFilteredLines( search, 0, 10 );
    REQUIRE( markLines.has_value() );
    REQUIRE( markLines->size() == 3 );
    REQUIRE( markLines->at( 0 ).num == 0_lnum );
    REQUIRE( markLines->at( 1 ).num == 1_lnum );
    REQUIRE( markLines->at( 2 ).num == 5_lnum );

    // Marks added after the switch join the union right away: the mutation is
    // mirrored into every projection that has Marks visible
    REQUIRE( session->addMarks( info.id, { 6_lnum } ).has_value() );
    const auto immediate = session->getFilteredLineCount( search );
    REQUIRE( immediate.has_value() );
    REQUIRE( *immediate == 4_lcount );
    const auto remirrored = session->setSearchVisibility( search, true, true, false );
    REQUIRE( remirrored.has_value() );
    REQUIRE( *remirrored == 5_lcount );

    // Back to matches only
    const auto matchesCount = session->setSearchVisibility( search, true, false, false );
    REQUIRE( matchesCount.has_value() );
    REQUIRE( *matchesCount == 2_lcount );

    const auto matchLines = session->getFilteredLines( search, 0, 10 );
    REQUIRE( matchLines.has_value() );
    REQUIRE( matchLines->size() == 2 );
    REQUIRE( matchLines->at( 0 ).num == 1_lnum );
    REQUIRE( matchLines->at( 1 ).num == 3_lnum );
}

TEST_CASE( "API marks are mirrored into the searches that project them", "[api][projection]" )
{
    const QString longLine
        = QStringLiteral( "INFO l4 with a long tail that makes this line the longest one" );

    auto session = createSession();
    const auto file = writeTempLogFile(
        { "INFO l0", "WARN l1", "INFO l2", "WARN l3", longLine, "INFO l5", "INFO l6" } );
    const auto info = openSyncOrFail( *session, file.path );

    const auto search = runSearchOrFail( *session, info.id, "WARN" );
    REQUIRE( session->setSearchVisibility( search, true, true, false ).has_value() );

    const auto initial = session->getFilteredLineCount( search );
    REQUIRE( initial.has_value() );
    REQUIRE( *initial == 2_lcount );

    // A mark on a non-matching line joins the union without a visibility flip,
    // and its length feeds the filtered max right away
    REQUIRE( session->addMarks( info.id, { 4_lnum } ).has_value() );
    const auto afterAdd = session->getFilteredLineCount( search );
    REQUIRE( afterAdd.has_value() );
    REQUIRE( *afterAdd == 3_lcount );

    const auto withMark = session->getFilteredLines( search, 0, 10 );
    REQUIRE( withMark.has_value() );
    REQUIRE( withMark->size() == 3 );
    REQUIRE( withMark->at( 2 ).num == 4_lnum );

    const auto maxWithMark = session->filteredMaxLineLength( search );
    REQUIRE( maxWithMark.has_value() );
    REQUIRE( *maxWithMark == LineLength( longLine.size() ) );

    // Removing it drops the line again, leaving the matches untouched
    REQUIRE( session->removeMarks( info.id, { 4_lnum } ).has_value() );
    const auto afterRemove = session->getFilteredLineCount( search );
    REQUIRE( afterRemove.has_value() );
    REQUIRE( *afterRemove == 2_lcount );

    // The cached longest mark is recomputed, so the max falls back to the
    // matches (the exact value is platform dependent: the search side measures
    // raw line bytes, which include a CRLF file's '\r')
    const auto maxAfterRemove = session->filteredMaxLineLength( search );
    REQUIRE( maxAfterRemove.has_value() );
    REQUIRE( *maxAfterRemove < *maxWithMark );
    REQUIRE( *maxAfterRemove >= LineLength( QStringLiteral( "WARN l1" ).size() ) );

    // A projection without Marks is not rebuilt by mark changes
    REQUIRE( session->setSearchVisibility( search, true, false, false ).has_value() );
    REQUIRE( session->addMarks( info.id, { 5_lnum } ).has_value() );
    const auto marksHidden = session->getFilteredLineCount( search );
    REQUIRE( marksHidden.has_value() );
    REQUIRE( *marksHidden == 2_lcount );
    REQUIRE( session->removeMarks( info.id, { 5_lnum } ).has_value() );
    const auto restored = session->getFilteredLineCount( search );
    REQUIRE( restored.has_value() );
    REQUIRE( *restored == 2_lcount );

    // Every Marks-visible search of the file mirrors the same mutation
    const auto secondSearch = runSearchOrFail( *session, info.id, "INFO" );
    REQUIRE( session->setSearchVisibility( search, true, true, false ).has_value() );
    REQUIRE( session->setSearchVisibility( secondSearch, true, true, false ).has_value() );
    REQUIRE( session->addMarks( info.id, { 2_lnum } ).has_value() );

    const auto firstCount = session->getFilteredLineCount( search );
    REQUIRE( firstCount.has_value() );
    REQUIRE( *firstCount == 3_lcount );
    const auto secondCount = session->getFilteredLineCount( secondSearch );
    REQUIRE( secondCount.has_value() );
    REQUIRE( *secondCount == 5_lcount );

    REQUIRE( session->removeMarks( info.id, { 2_lnum } ).has_value() );
    const auto firstRestored = session->getFilteredLineCount( search );
    REQUIRE( firstRestored.has_value() );
    REQUIRE( *firstRestored == 2_lcount );
    const auto secondRestored = session->getFilteredLineCount( secondSearch );
    REQUIRE( secondRestored.has_value() );
    REQUIRE( *secondRestored == 5_lcount );
}

TEST_CASE( "API filteredLineIndex ranks source lines in the visibility union", "[api][projection]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "INFO l0", "WARN l1", "INFO l2", "WARN l3", "INFO l4",
                                          "INFO l5", "INFO l6", "INFO l7", "INFO l8", "INFO l9" } );
    const auto info = openSyncOrFail( *session, file.path );

    const auto search = runSearchOrFail( *session, info.id, "WARN" );

    // Matches-only projection (the default): {1, 3}
    const auto firstMatch = session->filteredLineIndex( search, 1_lnum );
    REQUIRE( firstMatch.has_value() );
    REQUIRE( *firstMatch == 0 );
    const auto secondMatch = session->filteredLineIndex( search, 3_lnum );
    REQUIRE( secondMatch.has_value() );
    REQUIRE( *secondMatch == 1 );
    const auto notInMatches = session->filteredLineIndex( search, 0_lnum );
    REQUIRE( notInMatches.has_value() );
    REQUIRE( !notInMatches->has_value() );

    // Union with marks {0, 1, 5}: {0, 1, 3, 5} (the events half of the union
    // is exercised by the extraction-driven test in test_api_events.cpp)
    REQUIRE( session->addMarks( info.id, { 0_lnum, 1_lnum, 5_lnum } ).has_value() );

    const auto unionCount = session->setSearchVisibility( search, true, true, true );
    REQUIRE( unionCount.has_value() );
    REQUIRE( *unionCount == 4_lcount );

    for ( uint64_t line = 0; line < 10; ++line ) {
        const auto index = session->filteredLineIndex( search, LineNumber( line ) );
        REQUIRE( index.has_value() );
        CAPTURE( line );
        switch ( line ) {
        case 0:
        case 1:
            REQUIRE( **index == line );
            break;
        case 3:
            REQUIRE( **index == 2 );
            break;
        case 5:
            REQUIRE( **index == 3 );
            break;
        default:
            // Not in the projection
            REQUIRE( !index->has_value() );
            break;
        }
    }

    // Back to matches only: union members from the marks class drop out
    REQUIRE( session->setSearchVisibility( search, true, false, false ).has_value() );
    const auto markOnlyLine = session->filteredLineIndex( search, 5_lnum );
    REQUIRE( markOnlyLine.has_value() );
    REQUIRE( !markOnlyLine->has_value() );
    const auto stillMatch = session->filteredLineIndex( search, 3_lnum );
    REQUIRE( stillMatch.has_value() );
    REQUIRE( *stillMatch == 1 );

    // Unknown and cancelled searches
    const auto unknown = session->filteredLineIndex( 424242, 0_lnum );
    REQUIRE( !unknown.has_value() );
    REQUIRE( unknown.error().code == ErrorCode::InvalidSearchId );

    REQUIRE( session->cancelSearch( search ).has_value() );
    const auto cancelled = session->filteredLineIndex( search, 1_lnum );
    REQUIRE( !cancelled.has_value() );
    REQUIRE( cancelled.error().code == ErrorCode::SearchCancelled );
}

TEST_CASE( "API interruptSearch stops the search but keeps results readable",
           "[api][projection]" )
{
    auto session = createSession();
    const auto file
        = writeTempLogFile( { "INFO l0", "WARN l1", "INFO l2", "WARN l3", "INFO l4" } );
    const auto info = openSyncOrFail( *session, file.path );

    const auto search = runSearchOrFail( *session, info.id, "WARN" );
    REQUIRE( session->interruptSearch( search ).has_value() );

    // The id stays valid: progress, count, projection and mapping still work
    const auto progress = session->searchProgress( search );
    REQUIRE( progress.has_value() );
    REQUIRE( progress->total_matches == 2_lcount );

    const auto count = session->getFilteredLineCount( search );
    REQUIRE( count.has_value() );
    REQUIRE( *count == 2_lcount );

    const auto projection = session->getFilteredLines( search, 0, 10 );
    REQUIRE( projection.has_value() );
    REQUIRE( projection->size() == 2 );

    const auto index = session->filteredLineIndex( search, 3_lnum );
    REQUIRE( index.has_value() );
    REQUIRE( *index == 1 );

    // Visibility switching keeps working on an interrupted search
    REQUIRE( session->addMarks( info.id, { 0_lnum } ).has_value() );
    const auto unionCount = session->setSearchVisibility( search, true, true, false );
    REQUIRE( unionCount.has_value() );
    REQUIRE( *unionCount == 3_lcount );

    // A later cancel still drops everything
    REQUIRE( session->cancelSearch( search ).has_value() );
    const auto cancelled = session->getFilteredLineCount( search );
    REQUIRE( !cancelled.has_value() );
    REQUIRE( cancelled.error().code == ErrorCode::SearchCancelled );

    // Unknown id is a no-op, same as cancel
    REQUIRE( session->interruptSearch( 424242 ).has_value() );
}

TEST_CASE( "API getLinesBatch fetches arbitrary lines in request order", "[api][projection]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "alpha", "beta", "gamma", "delta" } );
    const auto info = openSyncOrFail( *session, file.path );

    const std::vector<LineNumber> lines{ 3_lnum, 0_lnum, 2_lnum, 3_lnum };
    const auto result = session->getLinesBatch( info.id, lines );
    REQUIRE( result.has_value() );
    REQUIRE( result->size() == 4 );
    REQUIRE( result->at( 0 ).num == 3_lnum );
    REQUIRE( result->at( 0 ).text == "delta" );
    REQUIRE( result->at( 1 ).num == 0_lnum );
    REQUIRE( result->at( 1 ).text == "alpha" );
    REQUIRE( result->at( 2 ).num == 2_lnum );
    REQUIRE( result->at( 2 ).text == "gamma" );
    REQUIRE( result->at( 3 ).text == "delta" );

    // Out-of-file lines yield the engine's padding line instead of failing
    const auto pastEnd = session->getLinesBatch( info.id, { 1_lnum, 100_lnum } );
    REQUIRE( pastEnd.has_value() );
    REQUIRE( pastEnd->size() == 2 );
    REQUIRE( pastEnd->at( 0 ).text == "beta" );
    REQUIRE( pastEnd->at( 1 ).text.find( "KLOGG WARNING" ) != std::string::npos );

    const auto empty = session->getLinesBatch( info.id, {} );
    REQUIRE( empty.has_value() );
    REQUIRE( empty->empty() );

    const auto unknown = session->getLinesBatch( 424242, { 0_lnum } );
    REQUIRE( !unknown.has_value() );
    REQUIRE( unknown.error().code == ErrorCode::FileNotOpen );
}

TEST_CASE( "API filteredLineNumbers enumerates the union projection without text",
           "[api][projection]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile(
        { "INFO l0", "WARN l1", "INFO l2", "WARN l3", "INFO l4", "INFO l5", "INFO l6" } );
    const auto info = openSyncOrFail( *session, file.path );

    const auto search = runSearchOrFail( *session, info.id, "WARN" );

    // Matches-only projection (the default): {1, 3}
    const auto matches = session->filteredLineNumbers( search, 0, 10 );
    REQUIRE( matches.has_value() );
    REQUIRE( matches->size() == 2 );
    REQUIRE( matches->at( 0 ) == 1_lnum );
    REQUIRE( matches->at( 1 ) == 3_lnum );

    // Union with marks {0, 1, 5}: {0, 1, 3, 5}; line numbers identical to the
    // text-carrying variant
    REQUIRE( session->addMarks( info.id, { 0_lnum, 1_lnum, 5_lnum } ).has_value() );
    REQUIRE( session->setSearchVisibility( search, true, true, false ).has_value() );

    const auto unionLines = session->filteredLineNumbers( search, 0, 10 );
    REQUIRE( unionLines.has_value() );
    REQUIRE( unionLines->size() == 4 );
    const auto withText = session->getFilteredLines( search, 0, 10 );
    REQUIRE( withText.has_value() );
    REQUIRE( withText->size() == unionLines->size() );
    for ( size_t i = 0; i < unionLines->size(); ++i ) {
        REQUIRE( unionLines->at( i ) == withText->at( i ).num );
    }

    // Paged reads reassemble to the whole projection (page of 3 over 4 lines
    // exercises the boundary: full page, then short page)
    std::vector<LineNumber> paged;
    for ( uint64_t from = 0;; ) {
        const auto page = session->filteredLineNumbers( search, from, 3 );
        REQUIRE( page.has_value() );
        paged.insert( paged.end(), page->begin(), page->end() );
        from += page->size();
        if ( page->size() < 3 ) {
            break;
        }
    }
    REQUIRE( paged == *unionLines );

    // Out-of-range and zero-count reads are empty, not errors
    const auto pastEnd = session->filteredLineNumbers( search, 4, 10 );
    REQUIRE( pastEnd.has_value() );
    REQUIRE( pastEnd->empty() );
    const auto zeroCount = session->filteredLineNumbers( search, 0, 0 );
    REQUIRE( zeroCount.has_value() );
    REQUIRE( zeroCount->empty() );

    const auto unknown = session->filteredLineNumbers( 424242, 0, 10 );
    REQUIRE( !unknown.has_value() );
    REQUIRE( unknown.error().code == ErrorCode::InvalidSearchId );

    REQUIRE( session->cancelSearch( search ).has_value() );
    const auto cancelled = session->filteredLineNumbers( search, 0, 10 );
    REQUIRE( !cancelled.has_value() );
    REQUIRE( cancelled.error().code == ErrorCode::SearchCancelled );
}

TEST_CASE( "API filteredLineIndexAtOrBefore floors to the previous union member",
           "[api][projection]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile(
        { "INFO l0", "WARN l1", "INFO l2", "WARN l3", "INFO l4", "INFO l5", "INFO l6" } );
    const auto info = openSyncOrFail( *session, file.path );

    const auto search = runSearchOrFail( *session, info.id, "WARN" );

    // Matches-only projection {1, 3}: a line before every member floors to
    // nothing (the native variant clamps it to 0)
    const auto beforeAll = session->filteredLineIndexAtOrBefore( search, 0_lnum );
    REQUIRE( beforeAll.has_value() );
    REQUIRE( !beforeAll->has_value() );

    // Union with marks {0, 5}: {0, 1, 3, 5}
    REQUIRE( session->addMarks( info.id, { 0_lnum, 5_lnum } ).has_value() );
    REQUIRE( session->setSearchVisibility( search, true, true, false ).has_value() );

    const std::vector<std::pair<uint64_t, uint64_t>> expectedFloors{
        { 0, 0 }, { 1, 1 }, { 2, 1 }, { 3, 2 }, { 4, 2 }, { 5, 3 }, { 6, 3 },
    };
    for ( const auto& [ line, expected ] : expectedFloors ) {
        const auto index = session->filteredLineIndexAtOrBefore( search, LineNumber( line ) );
        CAPTURE( line );
        REQUIRE( index.has_value() );
        REQUIRE( index->has_value() );
        REQUIRE( **index == expected );
    }

    // Past the last member floors to the last member
    const auto pastEnd = session->filteredLineIndexAtOrBefore( search, 100_lnum );
    REQUIRE( pastEnd.has_value() );
    REQUIRE( pastEnd->has_value() );
    REQUIRE( **pastEnd == 3 );

    // Unknown and cancelled searches
    const auto unknown = session->filteredLineIndexAtOrBefore( 424242, 0_lnum );
    REQUIRE( !unknown.has_value() );
    REQUIRE( unknown.error().code == ErrorCode::InvalidSearchId );

    REQUIRE( session->cancelSearch( search ).has_value() );
    const auto cancelled = session->filteredLineIndexAtOrBefore( search, 1_lnum );
    REQUIRE( !cancelled.has_value() );
    REQUIRE( cancelled.error().code == ErrorCode::SearchCancelled );
}

TEST_CASE( "API filteredMaxLineLength matches the native filtered-set max", "[api][projection]" )
{
    const QString matchLine = QStringLiteral( "WARN this is the longest matching line" );
    const QString markLine
        = QStringLiteral( "INFO and this marked line is the longest of them all" );
    const QString eventLine = QStringLiteral( "INFO this event line is fairly long too" );

    auto session = createSession();
    const auto file = writeTempLogFile( { "x", matchLine, "tiny", markLine, eventLine } );
    const auto info = openSyncOrFail( *session, file.path );

    const auto search = runSearchOrFail( *session, info.id, "WARN" );

    const auto matchesOnly = session->filteredMaxLineLength( search );
    REQUIRE( matchesOnly.has_value() );
    // Filtered-set max, not the full-file max (no absolute match-side check:
    // the search worker counts the raw line bytes, so a CRLF file's matches
    // include the '\r' that getLineLength strips)
    REQUIRE( *matchesOnly >= LineLength( matchLine.size() ) );
    REQUIRE( *matchesOnly < info.max_line_length );

    // Marks join the max regardless of the visibility switch (events join
    // through the engine-side extraction sync, covered in test_api_events)
    REQUIRE( session->addMarks( info.id, { 3_lnum } ).has_value() );
    REQUIRE( session->setSearchVisibility( search, true, true, true ).has_value() );

    const auto withMarksAndEvents = session->filteredMaxLineLength( search );
    REQUIRE( withMarksAndEvents.has_value() );
    REQUIRE( *withMarksAndEvents == LineLength( markLine.size() ) );

    const auto unknown = session->filteredMaxLineLength( 424242 );
    REQUIRE( !unknown.has_value() );
    REQUIRE( unknown.error().code == ErrorCode::InvalidSearchId );

    REQUIRE( session->cancelSearch( search ).has_value() );
    const auto cancelled = session->filteredMaxLineLength( search );
    REQUIRE( !cancelled.has_value() );
    REQUIRE( cancelled.error().code == ErrorCode::SearchCancelled );
}

TEST_CASE( "API setSearchVisibility and getFilteredLineCount reject bad searches",
           "[api][projection]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "one", "two" } );
    const auto info = openSyncOrFail( *session, file.path );

    const auto unknownVisibility = session->setSearchVisibility( 424242, true, false, false );
    REQUIRE( !unknownVisibility.has_value() );
    REQUIRE( unknownVisibility.error().code == ErrorCode::InvalidSearchId );

    const auto unknownCount = session->getFilteredLineCount( 424242 );
    REQUIRE( !unknownCount.has_value() );
    REQUIRE( unknownCount.error().code == ErrorCode::InvalidSearchId );

    const auto search = runSearchOrFail( *session, info.id, "one" );
    REQUIRE( session->cancelSearch( search ).has_value() );

    const auto cancelledVisibility = session->setSearchVisibility( search, true, true, false );
    REQUIRE( !cancelledVisibility.has_value() );
    REQUIRE( cancelledVisibility.error().code == ErrorCode::SearchCancelled );

    const auto cancelledCount = session->getFilteredLineCount( search );
    REQUIRE( !cancelledCount.has_value() );
    REQUIRE( cancelledCount.error().code == ErrorCode::SearchCancelled );
}

TEST_CASE( "API lineFlags reports mark and match bits per source line", "[api][projection]" )
{
    auto session = createSession();
    const auto file
        = writeTempLogFile( { "INFO l0", "WARN l1", "INFO l2", "WARN l3", "INFO l4", "INFO l5" } );
    const auto info = openSyncOrFail( *session, file.path );

    // Matches at 1 and 3; marks at 3 and 4 (line 3 is both)
    const auto search = runSearchOrFail( *session, info.id, "WARN" );
    REQUIRE( session->addMarks( info.id, { 3_lnum, 4_lnum } ).has_value() );

    const auto flags = session->lineFlags( info.id, search, 0_lnum, 6_lcount );
    REQUIRE( flags.has_value() );
    REQUIRE( flags->size() == 6 );
    REQUIRE( flags->at( 0 ) == 0 );
    REQUIRE( flags->at( 1 ) == LineFlagMatch );
    REQUIRE( flags->at( 2 ) == 0 );
    REQUIRE( flags->at( 3 ) == ( LineFlagMark | LineFlagMatch ) );
    REQUIRE( flags->at( 4 ) == LineFlagMark );
    REQUIRE( flags->at( 5 ) == 0 );

    // No extraction has run for this file, so no line carries the event flag
    for ( const auto flagBits : *flags ) {
        REQUIRE( ( flagBits & LineFlagEvent ) == 0 );
    }

    // Without a search only mark bits are reported
    const auto marksOnly = session->lineFlags( info.id, std::nullopt, 0_lnum, 6_lcount );
    REQUIRE( marksOnly.has_value() );
    REQUIRE( marksOnly->size() == 6 );
    REQUIRE( marksOnly->at( 1 ) == 0 );
    REQUIRE( marksOnly->at( 3 ) == LineFlagMark );
    REQUIRE( marksOnly->at( 4 ) == LineFlagMark );

    // Ranges are clamped to the file size
    const auto clamped = session->lineFlags( info.id, search, 4_lnum, 10_lcount );
    REQUIRE( clamped.has_value() );
    REQUIRE( clamped->size() == 2 );
    REQUIRE( clamped->at( 0 ) == LineFlagMark );
    REQUIRE( clamped->at( 1 ) == 0 );

    const auto pastEnd = session->lineFlags( info.id, search, 6_lnum, 1_lcount );
    REQUIRE( pastEnd.has_value() );
    REQUIRE( pastEnd->empty() );

    const auto zeroCount = session->lineFlags( info.id, search, 0_lnum, 0_lcount );
    REQUIRE( zeroCount.has_value() );
    REQUIRE( zeroCount->empty() );
}

TEST_CASE( "API lineFlags rejects unknown ids and cancelled searches", "[api][projection]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "one", "two" } );
    const auto otherFile = writeTempLogFile( { "three" } );
    const auto info = openSyncOrFail( *session, file.path );
    const auto otherInfo = openSyncOrFail( *session, otherFile.path );

    const auto unknownFile = session->lineFlags( 424242, std::nullopt, 0_lnum, 1_lcount );
    REQUIRE( !unknownFile.has_value() );
    REQUIRE( unknownFile.error().code == ErrorCode::FileNotOpen );

    const auto unknownSearch = session->lineFlags( info.id, 424242, 0_lnum, 1_lcount );
    REQUIRE( !unknownSearch.has_value() );
    REQUIRE( unknownSearch.error().code == ErrorCode::InvalidSearchId );

    // A search of another file is invalid for this file's line flags
    const auto otherSearch = runSearchOrFail( *session, otherInfo.id, "three" );
    const auto crossFile = session->lineFlags( info.id, otherSearch, 0_lnum, 1_lcount );
    REQUIRE( !crossFile.has_value() );
    REQUIRE( crossFile.error().code == ErrorCode::InvalidSearchId );

    const auto search = runSearchOrFail( *session, info.id, "one" );
    REQUIRE( session->cancelSearch( search ).has_value() );
    const auto cancelled = session->lineFlags( info.id, search, 0_lnum, 1_lcount );
    REQUIRE( !cancelled.has_value() );
    REQUIRE( cancelled.error().code == ErrorCode::SearchCancelled );
}

TEST_CASE( "API lineFlagsBatch reports flags for arbitrary source lines", "[api][projection]" )
{
    auto session = createSession();
    const auto file
        = writeTempLogFile( { "INFO l0", "WARN l1", "INFO l2", "WARN l3", "INFO l4", "INFO l5" } );
    const auto info = openSyncOrFail( *session, file.path );

    // Matches at 1 and 3; marks at 3 and 4 (line 3 is both)
    const auto search = runSearchOrFail( *session, info.id, "WARN" );
    REQUIRE( session->addMarks( info.id, { 3_lnum, 4_lnum } ).has_value() );

    const std::vector<LineNumber> lines{ 5_lnum, 3_lnum, 1_lnum, 4_lnum, 0_lnum, 3_lnum };
    const auto flags = session->lineFlagsBatch( info.id, search, lines );
    REQUIRE( flags.has_value() );
    REQUIRE( flags->size() == lines.size() );
    // One byte per requested line, in request order
    REQUIRE( flags->at( 0 ) == 0 );
    REQUIRE( flags->at( 1 ) == ( LineFlagMark | LineFlagMatch ) );
    REQUIRE( flags->at( 2 ) == LineFlagMatch );
    REQUIRE( flags->at( 3 ) == LineFlagMark );
    REQUIRE( flags->at( 4 ) == 0 );
    REQUIRE( flags->at( 5 ) == ( LineFlagMark | LineFlagMatch ) );

    // Matches the range variant on a contiguous slice
    const auto range = session->lineFlags( info.id, search, 0_lnum, 6_lcount );
    REQUIRE( range.has_value() );
    const std::vector<LineNumber> all{ 0_lnum, 1_lnum, 2_lnum, 3_lnum, 4_lnum, 5_lnum };
    const auto batch = session->lineFlagsBatch( info.id, search, all );
    REQUIRE( batch.has_value() );
    REQUIRE( *batch == *range );

    // Out-of-file lines come back plain instead of failing the batch
    const auto pastEnd = session->lineFlagsBatch( info.id, search, { 6_lnum, 1_lnum } );
    REQUIRE( pastEnd.has_value() );
    REQUIRE( pastEnd->size() == 2 );
    REQUIRE( pastEnd->at( 0 ) == 0 );
    REQUIRE( pastEnd->at( 1 ) == LineFlagMatch );

    const auto empty = session->lineFlagsBatch( info.id, search, {} );
    REQUIRE( empty.has_value() );
    REQUIRE( empty->empty() );

    // Without a search only mark bits are reported
    const auto marksOnly = session->lineFlagsBatch( info.id, std::nullopt, { 1_lnum, 4_lnum } );
    REQUIRE( marksOnly.has_value() );
    REQUIRE( marksOnly->size() == 2 );
    REQUIRE( marksOnly->at( 0 ) == 0 );
    REQUIRE( marksOnly->at( 1 ) == LineFlagMark );
}

TEST_CASE( "API lineFlagsBatch rejects unknown ids and cancelled searches", "[api][projection]" )
{
    auto session = createSession();
    const auto file = writeTempLogFile( { "one", "two" } );
    const auto otherFile = writeTempLogFile( { "three" } );
    const auto info = openSyncOrFail( *session, file.path );
    const auto otherInfo = openSyncOrFail( *session, otherFile.path );

    const auto unknownFile = session->lineFlagsBatch( 424242, std::nullopt, { 0_lnum } );
    REQUIRE( !unknownFile.has_value() );
    REQUIRE( unknownFile.error().code == ErrorCode::FileNotOpen );

    const auto unknownSearch = session->lineFlagsBatch( info.id, 424242, { 0_lnum } );
    REQUIRE( !unknownSearch.has_value() );
    REQUIRE( unknownSearch.error().code == ErrorCode::InvalidSearchId );

    // A search of another file is invalid for this file's line flags
    const auto otherSearch = runSearchOrFail( *session, otherInfo.id, "three" );
    const auto crossFile = session->lineFlagsBatch( info.id, otherSearch, { 0_lnum } );
    REQUIRE( !crossFile.has_value() );
    REQUIRE( crossFile.error().code == ErrorCode::InvalidSearchId );

    const auto search = runSearchOrFail( *session, info.id, "one" );
    REQUIRE( session->cancelSearch( search ).has_value() );
    const auto cancelled = session->lineFlagsBatch( info.id, search, { 0_lnum } );
    REQUIRE( !cancelled.has_value() );
    REQUIRE( cancelled.error().code == ErrorCode::SearchCancelled );
}

TEST_CASE( "API per-line vs prefetched frame timing", "[api][projection][bench][.]" )
{
    constexpr uint64_t kFileLines = 20000;
    constexpr size_t kFrameLines = 100;
    constexpr int kRounds = 50;

    auto session = createSession();
    QStringList content;
    content.reserve( static_cast<int>( kFileLines ) );
    for ( uint64_t i = 0; i < kFileLines; ++i ) {
        content << ( i % 7 == 0 ? QString( "WARN line %1" ).arg( i )
                                : QString( "INFO line %1" ).arg( i ) );
    }
    const auto file = writeTempLogFile( content );
    const auto info = openSyncOrFail( *session, file.path );

    const auto search = runSearchOrFail( *session, info.id, "WARN" );

    std::vector<LineNumber> marks;
    for ( uint64_t line = 0; line < kFileLines; line += 11 ) {
        marks.emplace_back( line );
    }
    REQUIRE( session->addMarks( info.id, marks ).has_value() );

    // Marks-enabled union projection: the non-contiguous source line set a
    // filtered view frame maps through
    REQUIRE( session->setSearchVisibility( search, true, true, false ).has_value() );
    const auto projection
        = session->getFilteredLines( search, 2500, static_cast<uint32_t>( kFrameLines ) );
    REQUIRE( projection.has_value() );
    REQUIRE( projection->size() == kFrameLines );
    std::vector<LineNumber> frameSourceLines;
    frameSourceLines.reserve( projection->size() );
    for ( const auto& line : *projection ) {
        frameSourceLines.push_back( line.num );
    }

    const auto timeUs = []( const std::function<void()>& frame ) {
        frame(); // warm up
        QElapsedTimer timer;
        timer.start();
        for ( int i = 0; i < kRounds; ++i ) {
            frame();
        }
        return static_cast<double>( timer.nsecsElapsed() ) / 1000.0 / kRounds;
    };

    size_t sink = 0;

    // Per-line patterns: one session round-trip per view line (main view),
    // two for the filtered view (projection mapping + flags)
    const auto mainPerLineUs = timeUs( [ & ] {
        for ( uint64_t i = 0; i < kFrameLines; ++i ) {
            const auto flags
                = session->lineFlags( info.id, search, LineNumber{ 5000 + i }, 1_lcount );
            sink += flags.has_value() ? flags->size() : 0;
        }
    } );
    const auto filteredPerLineUs = timeUs( [ & ] {
        for ( uint64_t i = 0; i < kFrameLines; ++i ) {
            const auto lines = session->getFilteredLines( search, 2500 + i, 1 );
            if ( !lines || lines->empty() ) {
                continue;
            }
            const auto flags = session->lineFlags( info.id, search, lines->front().num, 1_lcount );
            sink += flags.has_value() ? flags->size() : 0;
        }
    } );

    // Prefetched patterns: a single round-trip covers the whole frame
    const auto mainPrefetchUs = timeUs( [ & ] {
        const auto flags
            = session->lineFlags( info.id, search, 5000_lnum, LinesCount{ kFrameLines } );
        sink += flags.has_value() ? flags->size() : 0;
    } );
    const auto filteredPrefetchUs = timeUs( [ & ] {
        const auto flags = session->lineFlagsBatch( info.id, search, frameSourceLines );
        sink += flags.has_value() ? flags->size() : 0;
    } );

    // The line-number gutter of the filtered view: one projection round-trip
    // per view line before, zero extra after (the frame reuses the source
    // line numbers of the projection fetch its text getLines already does)
    const auto lineNumbersPerLineUs = timeUs( [ & ] {
        for ( uint64_t i = 0; i < kFrameLines; ++i ) {
            const auto lines = session->getFilteredLines( search, 2500 + i, 1 );
            sink += lines.has_value() ? lines->size() : 0;
        }
    } );
    const auto lineNumbersSharedUs = timeUs( [ & ] {
        const auto lines
            = session->getFilteredLines( search, 2500, static_cast<uint32_t>( kFrameLines ) );
        sink += lines.has_value() ? lines->size() : 0;
    } );

    // Reference: one contiguous batch fetch from the source file
    const auto sourceContiguousUs = timeUs( [ & ] {
        const auto lines = session->getLines( info.id, 5000_lnum, LinesCount{ kFrameLines } );
        sink += lines.has_value() ? lines->size() : 0;
    } );

    std::cout << "\ngutter costs of a " << kFrameLines << "-line frame (avg of " << kRounds
              << " rounds):\n"
              << "  main view     per-line: " << mainPerLineUs
              << " us  prefetched range: " << mainPrefetchUs << " us\n"
              << "  filtered view per-line: " << filteredPerLineUs
              << " us  prefetched batch: " << filteredPrefetchUs << " us\n"
              << "  line numbers  per-line: " << lineNumbersPerLineUs
              << " us  frame-shared projection: " << lineNumbersSharedUs << " us\n"
              << "  source contiguous batch: " << sourceContiguousUs << " us" << std::endl;

    // The prefetched bytes are identical to the per-line results
    const auto range = session->lineFlags( info.id, search, 5000_lnum, LinesCount{ kFrameLines } );
    REQUIRE( range.has_value() );
    REQUIRE( range->size() == kFrameLines );
    for ( size_t i = 0; i < kFrameLines; ++i ) {
        const auto single = session->lineFlags( info.id, search, LineNumber{ 5000 + i }, 1_lcount );
        REQUIRE( single.has_value() );
        REQUIRE( single->front() == range->at( i ) );
    }

    const auto batch = session->lineFlagsBatch( info.id, search, frameSourceLines );
    REQUIRE( batch.has_value() );
    REQUIRE( batch->size() == kFrameLines );
    for ( size_t i = 0; i < kFrameLines; ++i ) {
        const auto single = session->lineFlags( info.id, search, frameSourceLines[ i ], 1_lcount );
        REQUIRE( single.has_value() );
        REQUIRE( single->front() == batch->at( i ) );
    }

    // The per-line projection mapping is identical to the frame-shared fetch
    for ( size_t i = 0; i < kFrameLines; ++i ) {
        const auto single = session->getFilteredLines( search, 2500 + i, 1 );
        REQUIRE( single.has_value() );
        REQUIRE( single->size() == 1 );
        REQUIRE( single->front().num == frameSourceLines[ i ] );
    }

    // The batched fetch returns the exact line contents
    const auto frameLines
        = session->getFilteredLines( search, 2500, static_cast<uint32_t>( kFrameLines ) );
    REQUIRE( frameLines.has_value() );
    REQUIRE( frameLines->size() == kFrameLines );
    for ( size_t i = 0; i < kFrameLines; ++i ) {
        const auto num = frameLines->at( i ).num.get();
        const auto expected
            = ( num % 7 == 0 ? std::string{ "WARN line " } : std::string{ "INFO line " } )
              + std::to_string( num );
        REQUIRE( frameLines->at( i ).text == expected );
    }

    REQUIRE( sink > 0 );
}
