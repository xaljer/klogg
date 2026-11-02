/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 */

#include <catch2/catch.hpp>

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTextStream>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "configuration.h"
#include "persistentinfo.h"

#include "klogg/api.h"

using namespace klogg::api;

namespace {

constexpr int kWaitTimeoutMs = 20000;

// The stores are shared with the UI tests, so a leftover highlight set would
// colour the frames these cases assert on
void clearStoredHighlights()
{
    PersistentInfo::getStore( app_settings{} ).remove( "HighlighterSetCollection" );
}

std::unique_ptr<Session> createSession()
{
    auto session = Session::create();
    REQUIRE( session.has_value() );
    return std::move( session.value() );
}

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
    file.path = QString( "%1/klogg_api_viewport_test_%2.log" )
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

bool waitForSearch( Session& session, SearchId searchId, int timeoutMs = kWaitTimeoutMs )
{
    for ( int waited = 0; waited < timeoutMs; waited += 10 ) {
        const auto progress = session.searchProgress( searchId );
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

// Palette the expectations below refer to
const klogg::Rgba TextColor{ 0x11, 0x22, 0x33, 0xff };
const klogg::Rgba BaseColor{ 0xaa, 0xbb, 0xcc, 0xff };
const klogg::Rgba DimmedColor{ 0x55, 0x55, 0x55, 0xff };
const klogg::Rgba SelectionText{ 0xee, 0xee, 0xee, 0xff };
const klogg::Rgba SelectionBack{ 0x00, 0x00, 0x88, 0xff };

ViewportOptions makeOptions()
{
    ViewportOptions options;
    options.visibleColumns = 80_length;
    options.searchStartIndex = 0_lnum;
    options.searchEndIndex = 100_lnum;
    options.firstDisplayLineNumber = 1_lnum;
    options.palette = klogg::render::FramePalette{ TextColor, BaseColor, DimmedColor, SelectionText,
                                                   SelectionBack };
    return options;
}

HtmlRenderOptions makeHtmlOptions()
{
    HtmlRenderOptions options;
    options.palette = makeOptions().palette;
    options.searchStartIndex = 0_lnum;
    options.searchEndIndex = 100_lnum;
    return options;
}

QStringList sampleLines()
{
    return { QStringLiteral( "2024-01-15 [ERROR] disk failure" ),
             QStringLiteral( "2024-01-15 [INFO] all good" ),
             QStringLiteral( "2024-01-15 [ERROR] another failure" ),
             QStringLiteral( "2024-01-15 [INFO] all good again" ) };
}

} // namespace

TEST_CASE( "renderViewport composes the visible lines of a file", "[api][viewport]" )
{
    const auto file = writeTempLogFile( sampleLines() );

    clearStoredHighlights();
    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );
    REQUIRE( opened->total_lines == 4_lcount );

    const auto frame
        = session->renderViewport( opened->id, std::nullopt, 0_lnum, 4_lcount, makeOptions() );
    REQUIRE( frame.has_value() );
    REQUIRE( frame->lines.size() == 4 );

    for ( size_t i = 0; i < frame->lines.size(); ++i ) {
        const auto& line = frame->lines[ i ];
        REQUIRE( line.text == sampleLines()[ static_cast<int>( i ) ].toStdString() );
        REQUIRE( line.lineNumber == LineNumber{ static_cast<LineNumber::UnderlyingType>( i ) } );
        REQUIRE( line.displayLineNumber
                 == LineNumber{ static_cast<LineNumber::UnderlyingType>( i ) + 1 } );
        REQUIRE( line.flags == 0 );
        REQUIRE( line.foreColor == TextColor );
        REQUIRE( line.backColor == BaseColor );
        REQUIRE( line.parts.size() == 1 );
        REQUIRE( line.chunks.empty() == false );
    }
}

TEST_CASE( "renderLinesHtml serializes arbitrary lines as clipboard html and text",
           "[api][viewport]" )
{
    const auto file = writeTempLogFile(
        { "line zero", "line one", "line two", "line three", "line four", "line five" } );

    clearStoredHighlights();
    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );

    // Non-contiguous and out of order: the output follows the request order
    const std::vector<LineNumber> lines{ 4_lnum, 1_lnum, 5_lnum };
    const auto rendered
        = session->renderLinesHtml( opened->id, std::nullopt, lines, makeHtmlOptions() );
    REQUIRE( rendered.has_value() );
    REQUIRE( rendered->text == "line four\nline one\nline five" );

    const std::string pre = "<pre style=\"font-family:monospace; white-space:pre; margin:0\">";
    REQUIRE( rendered->html.rfind( pre, 0 ) == 0 );
    REQUIRE( rendered->html.size() > pre.size() + 6 );
    REQUIRE( rendered->html.substr( rendered->html.size() - 6 ) == "</pre>" );
    // Plain palette-coloured text produces no spans, only escaped text
    REQUIRE( rendered->html.find( "<span" ) == std::string::npos );
    REQUIRE( rendered->html.find( "line four\nline one\nline five" ) != std::string::npos );

    // A line past the end of the file comes back as an empty row
    const auto pastEnd = session->renderLinesHtml( opened->id, std::nullopt, { 1_lnum, 9_lnum },
                                                   makeHtmlOptions() );
    REQUIRE( pastEnd.has_value() );
    REQUIRE( pastEnd->text == "line one\n" );

    // An empty request still returns the empty clipboard document
    const auto empty = session->renderLinesHtml( opened->id, std::nullopt, {}, makeHtmlOptions() );
    REQUIRE( empty.has_value() );
    REQUIRE( empty->text.empty() );
    REQUIRE( empty->html == pre + "</pre>" );

    const auto unknown
        = session->renderLinesHtml( 12345, std::nullopt, { 0_lnum }, makeHtmlOptions() );
    REQUIRE( !unknown.has_value() );
    REQUIRE( unknown.error().code == ErrorCode::FileNotOpen );
}

TEST_CASE( "renderLinesHtml escapes the html special characters of the copied lines",
           "[api][viewport]" )
{
    const auto file = writeTempLogFile( { "plain", "if (a < b && c > d) { x = \"q\"; }" } );

    clearStoredHighlights();
    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );

    const auto rendered
        = session->renderLinesHtml( opened->id, std::nullopt, { 1_lnum }, makeHtmlOptions() );
    REQUIRE( rendered.has_value() );
    REQUIRE( rendered->text == "if (a < b && c > d) { x = \"q\"; }" );
    REQUIRE( rendered->html.find( "&lt;" ) != std::string::npos );
    REQUIRE( rendered->html.find( "&amp;&amp;" ) != std::string::npos );
    REQUIRE( rendered->html.find( "&gt;" ) != std::string::npos );
    REQUIRE( rendered->html.find( "&quot;" ) != std::string::npos );
    // The line's own angle brackets never reach the document raw
    REQUIRE( rendered->html.find( "a < b" ) == std::string::npos );
    REQUIRE( rendered->html.find( "<span" ) == std::string::npos );
}

TEST_CASE( "renderLinesHtml applies the pattern highlight colours", "[api][viewport]" )
{
    const auto file = writeTempLogFile( { "disk failure on sda", "all good" } );

    clearStoredHighlights();
    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );

    klogg::render::PatternHighlightOptions pattern;
    pattern.pattern = "failure";
    pattern.caseSensitive = true;
    pattern.useRegex = false;
    pattern.foreColor = klogg::Rgba{ 0xff, 0xff, 0x00, 0xff };
    pattern.backColor = klogg::Rgba{ 0x00, 0x00, 0xff, 0xff };

    auto options = makeHtmlOptions();
    options.patternHighlight = pattern;
    const auto rendered = session->renderLinesHtml( opened->id, std::nullopt, { 0_lnum }, options );
    REQUIRE( rendered.has_value() );
    REQUIRE( rendered->html.find(
                 "<span style=\"color:#ffff00;background-color:#0000ff\">failure</span>" )
             != std::string::npos );
    REQUIRE( rendered->text == "disk failure on sda" );

    // A transparent match background only carries the foreground colour
    pattern.backColor = klogg::Rgba{};
    options.patternHighlight = pattern;
    const auto transparent
        = session->renderLinesHtml( opened->id, std::nullopt, { 0_lnum }, options );
    REQUIRE( transparent.has_value() );
    REQUIRE( transparent->html.find( "<span style=\"color:#ffff00\">failure</span>" )
             != std::string::npos );
    REQUIRE( transparent->html.find( "background-color" ) == std::string::npos );
}

TEST_CASE( "renderLinesHtml applies the colour labels of the session palette", "[api][viewport]" )
{
    const auto file = writeTempLogFile( { "ERROR disk full", "INFO all good" } );

    clearStoredHighlights();
    auto session = createSession();
    QuickHighlighterDef label;
    label.name = "alert";
    label.color = "#ffff0000";
    label.backColor = "#ff00ff00";
    REQUIRE( session->setQuickHighlighters( { label } ).has_value() );

    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );

    auto options = makeHtmlOptions();
    options.quickHighlighterWords = { { "ERROR" } };
    const auto rendered = session->renderLinesHtml( opened->id, std::nullopt, { 0_lnum }, options );
    REQUIRE( rendered.has_value() );
    REQUIRE(
        rendered->html.find( "<span style=\"color:#ff0000;background-color:#00ff00\">ERROR</span>" )
        != std::string::npos );
    REQUIRE( rendered->text == "ERROR disk full" );
}

TEST_CASE( "renderLinesHtml keeps the requested order and the ANSI colours", "[api][viewport]" )
{
    const auto file
        = writeTempLogFile( { QString::fromUtf8( "\x1b[31mred zero\x1b[0m" ), "one", "two", "three",
                              "four", QString::fromUtf8( "\x1b[32mgreen five\x1b[0m" ) } );

    clearStoredHighlights();
    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );

    auto options = makeHtmlOptions();
    options.ansiProcessing = AnsiProcessing::RenderColors;
    // Only line 0 is inside the search range; line 5 is outside it and keeps
    // just its ANSI colour
    options.searchStartIndex = 0_lnum;
    options.searchEndIndex = 1_lnum;

    const std::vector<LineNumber> lines{ 5_lnum, 0_lnum, 9_lnum };
    const auto rendered = session->renderLinesHtml( opened->id, std::nullopt, lines, options );
    REQUIRE( rendered.has_value() );
    // One row per requested line, the out-of-file one empty
    REQUIRE( rendered->text == "green five\nred zero\n" );
    REQUIRE( rendered->html.find( "<span style=\"color:#00cd00\">green five</span>" )
             != std::string::npos );
    REQUIRE( rendered->html.find( "<span style=\"color:#cd0000\">red zero</span>" )
             != std::string::npos );
}

TEST_CASE( "renderViewport reports the search match bits in the gutter flags", "[api][viewport]" )
{
    const auto file = writeTempLogFile( sampleLines() );

    clearStoredHighlights();
    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );

    SearchParams params;
    params.pattern = "ERROR";
    params.isRegex = false;
    const auto searchId = session->runSearch( opened->id, params );
    REQUIRE( searchId.has_value() );
    REQUIRE( waitForSearch( *session, *searchId ) );

    const auto frame = session->renderViewport( opened->id, std::optional<SearchId>( *searchId ),
                                                0_lnum, 4_lcount, makeOptions() );
    REQUIRE( frame.has_value() );
    REQUIRE( frame->lines.size() == 4 );

    REQUIRE( frame->lines[ 0 ].flags == LineFlagMatch );
    REQUIRE( frame->lines[ 1 ].flags == 0 );
    REQUIRE( frame->lines[ 2 ].flags == LineFlagMatch );
    REQUIRE( frame->lines[ 3 ].flags == 0 );
}

TEST_CASE( "renderViewport wraps and dims the lines", "[api][viewport]" )
{
    const auto file = writeTempLogFile( sampleLines() );

    clearStoredHighlights();
    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );

    auto options = makeOptions();
    options.wrapColumns = 10_lcol;
    options.visibleColumns = 10_length;
    options.searchStartIndex = 2_lnum;
    options.searchEndIndex = 3_lnum;

    const auto frame
        = session->renderViewport( opened->id, std::nullopt, 0_lnum, 3_lcount, options );
    REQUIRE( frame.has_value() );
    REQUIRE( frame->lines.size() == 3 );

    // "2024-01-15 [ERROR] disk failure" is 32 columns wide
    REQUIRE( frame->lines[ 0 ].parts.size() == 4 );

    // Outside the search range the text is dimmed; the last line is inside it
    REQUIRE( frame->lines[ 0 ].foreColor == DimmedColor );
    REQUIRE( frame->lines[ 1 ].foreColor == DimmedColor );
    REQUIRE( frame->lines[ 2 ].foreColor == TextColor );
}

TEST_CASE( "renderViewport keeps one entry per requested line past the end", "[api][viewport]" )
{
    const auto file = writeTempLogFile( sampleLines() );

    clearStoredHighlights();
    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );

    const auto frame
        = session->renderViewport( opened->id, std::nullopt, 100_lnum, 3_lcount, makeOptions() );
    REQUIRE( frame.has_value() );
    REQUIRE( frame->lines.size() == 3 );
    for ( const auto& line : frame->lines ) {
        REQUIRE( line.text.empty() );
        REQUIRE( line.flags == 0 );
    }
}

TEST_CASE( "renderViewport reports an unknown file", "[api][viewport]" )
{
    clearStoredHighlights();
    auto session = createSession();

    const auto frame
        = session->renderViewport( 12345, std::nullopt, 0_lnum, 4_lcount, makeOptions() );
    REQUIRE( !frame.has_value() );
    REQUIRE( frame.error().code == ErrorCode::FileNotOpen );
}

TEST_CASE( "renderViewport sees the highlight state published by a mutation", "[api][viewport]" )
{
    const auto file = writeTempLogFile( sampleLines() );

    clearStoredHighlights();
    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );

    const auto before
        = session->renderViewport( opened->id, std::nullopt, 0_lnum, 4_lcount, makeOptions() );
    REQUIRE( before.has_value() );
    REQUIRE( before->lines[ 0 ].backColor == BaseColor );

    // A whole line rule with an explicit background: the row colour must follow
    // it, which is what the new paint path reads back
    HighlightRule rule{};
    rule.name = "errors";
    rule.pattern = "ERROR";
    rule.useRegex = false;
    rule.onlyMatch = false;
    rule.color = "#ffff0000";
    rule.backColor = "#ff204060";
    rule.useThemeBack = false;

    REQUIRE( session->setHighlighters( { rule } ).has_value() );

    const auto after
        = session->renderViewport( opened->id, std::nullopt, 0_lnum, 4_lcount, makeOptions() );
    REQUIRE( after.has_value() );
    REQUIRE( after->lines[ 0 ].backColor == klogg::Rgba{ 0x20, 0x40, 0x60, 0xff } );
    REQUIRE( after->lines[ 0 ].foreColor == klogg::Rgba{ 0xff, 0x00, 0x00, 0xff } );
    REQUIRE( after->lines[ 1 ].backColor == BaseColor );
    REQUIRE( after->lines[ 1 ].foreColor == TextColor );

    // ... and dropping the rules brings the palette back
    REQUIRE( session->setHighlighters( {} ).has_value() );

    const auto restored
        = session->renderViewport( opened->id, std::nullopt, 0_lnum, 4_lcount, makeOptions() );
    REQUIRE( restored.has_value() );
    REQUIRE( restored->lines[ 0 ].backColor == BaseColor );
    REQUIRE( restored->lines[ 0 ].foreColor == TextColor );
}

TEST_CASE( "renderViewport applies the pattern and selection colours", "[api][viewport]" )
{
    const auto file = writeTempLogFile( sampleLines() );

    clearStoredHighlights();
    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );

    auto options = makeOptions();

    klogg::render::PatternHighlightOptions pattern;
    pattern.pattern = "failure";
    pattern.caseSensitive = true;
    pattern.useRegex = false;
    pattern.foreColor = klogg::Rgba{ 0xff, 0xff, 0x00, 0xff };
    pattern.backColor = klogg::Rgba{ 0x00, 0x00, 0xff, 0xff };
    options.patternHighlight = pattern;

    klogg::render::QuickFindOptions quickFind;
    quickFind.pattern = "disk";
    quickFind.caseSensitive = true;
    quickFind.regex = false;
    quickFind.backColor = klogg::Rgba{ 0x00, 0xff, 0x00, 0xff };
    options.quickFind = quickFind;

    // "all good" of the second line, which starts at column 18
    klogg::render::SelectionPortion portion;
    portion.lineIndex = 1;
    portion.startColumn = 18_lcol;
    portion.length = 8_length;
    options.selectionPortions = { portion };

    const auto frame
        = session->renderViewport( opened->id, std::nullopt, 0_lnum, 4_lcount, options );
    REQUIRE( frame.has_value() );
    REQUIRE( frame->lines.size() == 4 );

    // "disk failure" is both a QuickFind hit and a pattern match on the first
    // line, so the pattern, which is applied later, wins there
    const auto& firstLine = frame->lines[ 0 ].chunks;
    const auto patternChunk = std::find_if(
        firstLine.begin(), firstLine.end(), []( const klogg::render::LineChunk& chunk ) {
            return chunk.backColor == klogg::Rgba{ 0x00, 0x00, 0xff, 0xff };
        } );
    REQUIRE( patternChunk != firstLine.end() );

    // The selection portion of the second line is applied last
    const auto& secondLine = frame->lines[ 1 ].chunks;
    const auto selectionChunk = std::find_if(
        secondLine.begin(), secondLine.end(),
        []( const klogg::render::LineChunk& chunk ) { return chunk.backColor == SelectionBack; } );
    REQUIRE( selectionChunk != secondLine.end() );
    REQUIRE( selectionChunk->length == 8_length );
    REQUIRE( selectionChunk->foreColor == SelectionText );
}
