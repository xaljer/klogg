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
#include <QUuid>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "persistentinfo.h"
#include "paths.h"
#include "userdata.h"

#include "klogg/api.h"

using namespace klogg::api;

namespace {

// Sessions load their state from the shared settings stores; wipe the groups
// used by the API so every case starts from a clean slate
void clearPersistedState()
{
    auto& appStore = PersistentInfo::getStore( app_settings{} );
    appStore.remove( "HighlighterSetCollection" );
    appStore.remove( "PredefinedFiltersCollection" );
    appStore.remove( "NamedSavedSearches" );

    auto& sessionStore = PersistentInfo::getStore( session_settings{} );
    sessionStore.remove( "SavedSearches" );
    sessionStore.remove( "RecentFiles" );
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
    file.path = QString( "%1/klogg_api_state_test_%2.log" )
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

std::vector<uint64_t> markValues( const std::vector<LineNumber>& marks )
{
    std::vector<uint64_t> values;
    values.reserve( marks.size() );
    for ( const auto mark : marks ) {
        values.push_back( mark.get() );
    }
    return values;
}

const klogg::Rgba TextColor{ 0x11, 0x22, 0x33, 0xff };
const klogg::Rgba BaseColor{ 0xaa, 0xbb, 0xcc, 0xff };
const klogg::Rgba DimmedColor{ 0x55, 0x55, 0x55, 0xff };
const klogg::Rgba SelectionText{ 0xee, 0xee, 0xee, 0xff };
const klogg::Rgba SelectionBack{ 0x00, 0x00, 0x88, 0xff };

ViewportOptions makeViewportOptions()
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

bool hasChunkColors( const ViewportLine& line, const klogg::Rgba& fore, const klogg::Rgba& back )
{
    return std::any_of( line.chunks.begin(), line.chunks.end(),
                        [ & ]( const klogg::render::LineChunk& chunk ) {
                            return chunk.foreColor == fore && chunk.backColor == back;
                        } );
}

// A rule that paints the whole matching line
HighlightRule wholeLineRule( const std::string& pattern, const std::string& fore,
                             const std::string& back )
{
    HighlightRule rule{};
    rule.pattern = pattern;
    rule.useRegex = false;
    rule.color = fore;
    rule.backColor = back;
    rule.useThemeBack = false;
    return rule;
}

} // namespace

TEST_CASE( "setHighlighters replaces the effective rule list", "[api][state]" )
{
    clearPersistedState();
    auto file = writeTempLogFile( { "plain line", "ERROR here" } );
    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );

    // No rules: the palette shows through
    auto frame = session->renderViewport( opened->id, std::nullopt, 0_lnum, 2_lcount,
                                          makeViewportOptions() );
    REQUIRE( frame.has_value() );
    REQUIRE( frame->lines[ 1 ].backColor == BaseColor );

    REQUIRE( session->setHighlighters( { wholeLineRule( "ERROR", "#ffff0000", "#ff204060" ) } )
                 .has_value() );

    frame = session->renderViewport( opened->id, std::nullopt, 0_lnum, 2_lcount,
                                     makeViewportOptions() );
    REQUIRE( frame.has_value() );
    REQUIRE( frame->lines[ 0 ].backColor == BaseColor );
    REQUIRE( frame->lines[ 1 ].backColor == klogg::Rgba{ 0x20, 0x40, 0x60, 0xff } );
    REQUIRE( frame->lines[ 1 ].foreColor == klogg::Rgba{ 0xff, 0x00, 0x00, 0xff } );

    // Replace-all: only the new rule stays
    REQUIRE( session->setHighlighters( { wholeLineRule( "plain", "#ff00ff00", "#ff000080" ) } )
                 .has_value() );

    frame = session->renderViewport( opened->id, std::nullopt, 0_lnum, 2_lcount,
                                     makeViewportOptions() );
    REQUIRE( frame.has_value() );
    REQUIRE( frame->lines[ 0 ].backColor == klogg::Rgba{ 0x00, 0x00, 0x80, 0xff } );
    REQUIRE( frame->lines[ 1 ].backColor == BaseColor );

    // An empty list clears every rule
    REQUIRE( session->setHighlighters( {} ).has_value() );
    frame = session->renderViewport( opened->id, std::nullopt, 0_lnum, 2_lcount,
                                     makeViewportOptions() );
    REQUIRE( frame.has_value() );
    REQUIRE( frame->lines[ 0 ].backColor == BaseColor );
}

TEST_CASE( "setQuickHighlighters replaces the colour label palette", "[api][state]" )
{
    clearPersistedState();
    auto file = writeTempLogFile( { "boot disk ready" } );
    auto session = createSession();
    const auto opened = session->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );

    QuickHighlighterDef slot;
    slot.name = "labels";
    slot.color = "#ff00ff00";
    slot.backColor = "#ffff0000";
    slot.useInCycle = true;
    REQUIRE( session->setQuickHighlighters( { slot } ).has_value() );

    auto options = makeViewportOptions();
    options.quickHighlighterWords = { { "disk" } };
    const auto frame
        = session->renderViewport( opened->id, std::nullopt, 0_lnum, 1_lcount, options );
    REQUIRE( frame.has_value() );
    REQUIRE( hasChunkColors( frame->lines[ 0 ], klogg::Rgba{ 0x00, 0xff, 0x00, 0xff },
                             klogg::Rgba{ 0xff, 0x00, 0x00, 0xff } ) );

    // Replacing the palette changes the colour the label paints with
    slot.color = "#ff0000ff";
    slot.backColor = "#ffffff00";
    REQUIRE( session->setQuickHighlighters( { slot } ).has_value() );

    const auto recolored
        = session->renderViewport( opened->id, std::nullopt, 0_lnum, 1_lcount, options );
    REQUIRE( recolored.has_value() );
    REQUIRE( hasChunkColors( recolored->lines[ 0 ], klogg::Rgba{ 0x00, 0x00, 0xff, 0xff },
                             klogg::Rgba{ 0xff, 0xff, 0x00, 0xff } ) );
}

TEST_CASE( "session json round trips the effective rules", "[api][state]" )
{
    clearPersistedState();

    HighlightRule rule{};
    rule.name = "persisted";
    rule.pattern = "WARN";
    rule.caseSensitive = false;
    rule.color = "#ff123456";
    rule.backColor = "#ff654321";
    rule.useThemeBack = false;
    rule.onlyMatch = true;
    rule.variateColors = false;
    rule.colorVariance = 25;
    rule.enabled = true;

    std::string json;
    {
        auto session = createSession();
        REQUIRE( session->setHighlighters( { rule } ).has_value() );
        const auto saved = session->saveSession();
        REQUIRE( saved.has_value() );
        json = *saved;
    }

    auto restored = createSession();
    REQUIRE( restored->restoreSession( json ).has_value() );

    // The restored rule colours the matched text, which is what the saved
    // fields carry
    auto file = writeTempLogFile( { "WARN: low disk" } );
    const auto opened = restored->openFileSync( file.path.toStdString() );
    REQUIRE( opened.has_value() );
    const auto frame = restored->renderViewport( opened->id, std::nullopt, 0_lnum, 1_lcount,
                                                 makeViewportOptions() );
    REQUIRE( frame.has_value() );
    REQUIRE( hasChunkColors( frame->lines[ 0 ], klogg::Rgba{ 0x12, 0x34, 0x56, 0xff },
                             klogg::Rgba{ 0x65, 0x43, 0x21, 0xff } ) );
}

TEST_CASE( "marks add/list/remove", "[api][state]" )
{
    clearPersistedState();
    auto session = createSession();
    auto file = writeTempLogFile( { "one", "two", "three", "four", "five", "six" } );

    const auto info = session->openFileSync( file.path.toStdString() );
    REQUIRE( info.has_value() );

    REQUIRE( session->addMarks( info->id, { LineNumber( 5 ), LineNumber( 2 ), LineNumber( 3 ) } )
                 .has_value() );
    // Idempotent re-add
    REQUIRE( session->addMarks( info->id, { LineNumber( 3 ) } ).has_value() );

    auto marks = session->listMarks( info->id );
    REQUIRE( marks.has_value() );
    REQUIRE( markValues( *marks ) == std::vector<uint64_t>{ 2, 3, 5 } );

    REQUIRE( session->removeMarks( info->id, { LineNumber( 3 ), LineNumber( 99 ) } ).has_value() );
    marks = session->listMarks( info->id );
    REQUIRE( markValues( *marks ) == std::vector<uint64_t>{ 2, 5 } );

    REQUIRE( !session->listMarks( 424242 ).has_value() );
}

TEST_CASE( "save/restore session round trip", "[api][state]" )
{
    clearPersistedState();
    auto file = writeTempLogFile( { "one", "ERROR two", "three", "four" } );

    std::string json;
    {
        auto session = createSession();

        REQUIRE( session->setHighlighters( { wholeLineRule( "ERROR", "#ffff0000",
                                                            "#ff204060" ) } )
                     .has_value() );

        const auto info = session->openFileSync( file.path.toStdString() );
        REQUIRE( info.has_value() );
        REQUIRE( session->addMarks( info->id, { LineNumber( 1 ), LineNumber( 3 ) } ).has_value() );

        const auto saved = session->saveSession();
        REQUIRE( saved.has_value() );
        json = *saved;
    }

    auto restored = createSession();
    REQUIRE( restored->restoreSession( json ).has_value() );

    const auto files = restored->openFiles();
    REQUIRE( files.has_value() );
    REQUIRE( files->size() == 1 );
    REQUIRE( ( *files )[ 0 ].path == file.path.toStdString() );

    // The restored rules still colour the ERROR line
    const auto frame = restored->renderViewport( ( *files )[ 0 ].id, std::nullopt, 0_lnum, 4_lcount,
                                                 makeViewportOptions() );
    REQUIRE( frame.has_value() );
    REQUIRE( frame->lines[ 1 ].backColor == klogg::Rgba{ 0x20, 0x40, 0x60, 0xff } );
    REQUIRE( frame->lines[ 1 ].foreColor == klogg::Rgba{ 0xff, 0x00, 0x00, 0xff } );
    REQUIRE( frame->lines[ 0 ].backColor == BaseColor );

    const auto marks = restored->listMarks( ( *files )[ 0 ].id );
    REQUIRE( marks.has_value() );
    REQUIRE( markValues( *marks ) == std::vector<uint64_t>{ 1, 3 } );
}

TEST_CASE( "restore session skips missing files", "[api][state]" )
{
    clearPersistedState();
    auto file = writeTempLogFile( { "line" } );
    const QString missing = QDir::tempPath() + "/klogg_api_state_missing_"
                            + QUuid::createUuid().toString( QUuid::WithoutBraces ) + ".log";

    const std::string json
        = R"JSON({ "version": 1, "files": [ { "path": ")JSON" + missing.toStdString()
          + R"JSON(", "encoding": "", "marks": [] }, { "path": ")JSON" + file.path.toStdString()
          + R"JSON(", "encoding": "", "marks": [ 0 ] } ] })JSON";

    auto session = createSession();
    const auto result = session->restoreSession( json );
    REQUIRE( !result.has_value() );
    REQUIRE( result.error().code == ErrorCode::InternalError );
    REQUIRE( result.error().message.find( missing.toStdString() ) != std::string::npos );

    // The good file was still restored, with its marks
    const auto files = session->openFiles();
    REQUIRE( files.has_value() );
    REQUIRE( files->size() == 1 );
    REQUIRE( ( *files )[ 0 ].path == file.path.toStdString() );
    const auto marks = session->listMarks( ( *files )[ 0 ].id );
    REQUIRE( markValues( *marks ) == std::vector<uint64_t>{ 0 } );
}

TEST_CASE( "highlight state isolation between sessions", "[api][state]" )
{
    clearPersistedState();
    auto file = writeTempLogFile( { "ERROR line" } );
    auto sessionA = createSession();
    auto sessionB = createSession();

    REQUIRE( sessionA->setHighlighters( { wholeLineRule( "ERROR", "#ffff0000", "#ff204060" ) } )
                 .has_value() );

    // B was created before the mutation and keeps its own in-memory rules
    const auto openedB = sessionB->openFileSync( file.path.toStdString() );
    REQUIRE( openedB.has_value() );
    const auto frameB = sessionB->renderViewport( openedB->id, std::nullopt, 0_lnum, 1_lcount,
                                                  makeViewportOptions() );
    REQUIRE( frameB.has_value() );
    REQUIRE( frameB->lines[ 0 ].backColor == BaseColor );

    const auto openedA = sessionA->openFileSync( file.path.toStdString() );
    REQUIRE( openedA.has_value() );
    const auto frameA = sessionA->renderViewport( openedA->id, std::nullopt, 0_lnum, 1_lcount,
                                                  makeViewportOptions() );
    REQUIRE( frameA.has_value() );
    REQUIRE( frameA->lines[ 0 ].backColor == klogg::Rgba{ 0x20, 0x40, 0x60, 0xff } );

    // The core never persists highlight state: a session created after the
    // mutation starts empty (the shell pushes the effective rules)
    auto sessionC = createSession();
    const auto openedC = sessionC->openFileSync( file.path.toStdString() );
    REQUIRE( openedC.has_value() );
    const auto frameC = sessionC->renderViewport( openedC->id, std::nullopt, 0_lnum, 1_lcount,
                                                  makeViewportOptions() );
    REQUIRE( frameC.has_value() );
    REQUIRE( frameC->lines[ 0 ].backColor == BaseColor );
}

// The session document is a persistence format: the exact bytes have to stay
// stable so an existing session file keeps loading. The reference below is the
// output captured from the QJsonDocument implementation this writer replaced.
TEST_CASE( "saveSession writes the session json byte for byte", "[api][state]" )
{
    clearPersistedState();

    // A relative name: the path lands in the document, and the reference has
    // to hold on every machine the suite runs on
    const std::string relativePath = "klogg_api_golden_state.log";
    {
        std::ofstream seed( relativePath, std::ios::binary | std::ios::trunc );
        seed << "alpha\nbeta\ngamma\ndelta\n";
    }

    HighlightRule rule{};
    rule.name = "esc \" quote \\ back \b \f \n \r \t \x01 \x1f \x7f \xc3\xa9 \xe4\xb8\xad "
                "\xf0\x9f\x98\x80";
    rule.pattern = "ERROR\\s+\\d+";
    rule.useRegex = false;
    rule.caseSensitive = false;
    rule.color = "#ff123456";
    rule.backColor = "#ff654321";
    rule.useThemeBack = false;
    rule.onlyMatch = true;
    rule.variateColors = true;
    rule.colorVariance = 25;
    rule.enabled = false;

    auto session = createSession();

    REQUIRE( session->setHighlighters( { rule } ).has_value() );

    const auto opened = session->openFileSync( relativePath );
    REQUIRE( opened.has_value() );
    // The codec is forced instead of detected: the document must not depend on
    // what the detector guesses for the same content elsewhere
    REQUIRE( session->setEncoding( opened->id, "ISO-8859-1" ).has_value() );
    REQUIRE( session->addMarks( opened->id, { LineNumber( 1 ), LineNumber( 3 ) } ).has_value() );

    const auto saved = session->saveSession();
    REQUIRE( saved.has_value() );

    // DEL is written as an escape so the reference stays printable
    static const std::string reference
        = R"REF({"files":[{"encoding":"ISO-8859-1","marks":[1,3],"path":"klogg_api_golden_state.log"}],"highlighters":[{"backColor":"#ff654321","caseSensitive":false,"color":"#ff123456","colorVariance":25,"enabled":false,"name":"esc \" quote \\ back \b \f \n \r \t \u0001 \u001f )REF"
          "\x7f"
          R"REF( é 中 😀","onlyMatch":true,"pattern":"ERROR\\s+\\d+","useRegex":false,"useThemeBack":false,"variateColors":true}],"version":1})REF";

    CHECK( *saved == reference );

    std::filesystem::remove( relativePath );
}

TEST_CASE( "platform store registry and PersistentInfo share the documents", "[api][state]" )
{
    // The Core API reads the shared state through the Qt-free platform
    // registry, the Qt layer keeps its PersistentInfo facade; both must hand
    // out the very same document or the UI and the engine would drift onto
    // different files
    REQUIRE( &PersistentInfo::getStore( app_settings{} ) == &platform::userdata::config() );
    REQUIRE( &PersistentInfo::getStore( session_settings{} ) == &platform::userdata::session() );

    REQUIRE( PersistentInfo::getConfigDir().toStdString()
             == platform::userdata::directory().string() );
    REQUIRE( platform::userdata::config().path().parent_path()
             == platform::userdata::directory() );
    REQUIRE( platform::userdata::config().path().filename().string() == "config.toml" );
    REQUIRE( platform::userdata::session().path().parent_path()
             == platform::userdata::directory() );
    REQUIRE( platform::userdata::session().path().filename().string() == "session.toml" );

    // This binary forces portable mode from its composition root, so the
    // documents must sit next to the executable: the location policy has to
    // reach the registry before the first store access
    REQUIRE( platform::userdata::directory() == platform::paths::executableDir() );
}
