#include <catch2/catch.hpp>

#include <QSettings>
#include <QTemporaryFile>

#include "configuration.h"
#include "persistentinfo.h"
#include "sessioninfo.h"

namespace {

struct ScopedTempSettings {
    ScopedTempSettings()
    {
        tempFile_.open();
        settings_ = std::make_unique<QSettings>( tempFile_.fileName(), QSettings::IniFormat );
    }

    QSettings& settings()
    {
        return *settings_;
    }

  private:
    QTemporaryFile tempFile_;
    std::unique_ptr<QSettings> settings_;
};

} // namespace

SCENARIO( "Configuration recorder commands serialization", "[configuration]" )
{
    GIVEN( "Configuration with recorder commands" )
    {
        Configuration config;
        ScopedTempSettings temp;

        QList<RecorderCommand> originalCommands;
        RecorderCommand cmd1;
        cmd1.name = "adb logcat";
        cmd1.command = "adb logcat > {file}";
        originalCommands.append( cmd1 );

        RecorderCommand cmd2;
        cmd2.name = "ssh tail";
        cmd2.command = "ssh server tail -f /var/log/app.log >> {file}";
        originalCommands.append( cmd2 );

        config.setRecorderCommands( originalCommands );

        WHEN( "Commands are saved and retrieved" )
        {
            config.saveToStorage( temp.settings() );

            Configuration restored;
            restored.retrieveFromStorage( temp.settings() );

            THEN( "All commands are preserved" )
            {
                const auto commands = restored.recorderCommands();
                REQUIRE( commands.size() == 2 );
                REQUIRE( commands[ 0 ].name == "adb logcat" );
                REQUIRE( commands[ 0 ].command == "adb logcat > {file}" );
                REQUIRE( commands[ 1 ].name == "ssh tail" );
                REQUIRE( commands[ 1 ].command
                         == "ssh server tail -f /var/log/app.log >> {file}" );
            }
        }
    }

    GIVEN( "Empty recorder commands list" )
    {
        Configuration config;
        ScopedTempSettings temp;

        config.setRecorderCommands( {} );
        config.saveToStorage( temp.settings() );

        WHEN( "Retrieved with no prior recorder commands" )
        {
            Configuration restored;
            restored.retrieveFromStorage( temp.settings() );

            THEN( "List is empty" )
            {
                const auto commands = restored.recorderCommands();
                REQUIRE( commands.isEmpty() );
            }
        }
    }
}

SCENARIO( "SessionInfo recorder command name persistence", "[session]" )
{
    GIVEN( "SessionInfo with open files that have recorder command names" )
    {
        ScopedTempSettings temp;

        const QString windowId = "test_window_recorder";
        SessionInfo sessionInfo;
        sessionInfo.add( windowId );

        std::vector<SessionInfo::OpenFile> openFiles;
        openFiles.push_back(
            SessionInfo::OpenFile( "D:/logs/app.log", 100, "context1", "adb logcat" ) );
        openFiles.push_back(
            SessionInfo::OpenFile( "D:/logs/server.log", 200, "context2", {} ) );
        openFiles.push_back(
            SessionInfo::OpenFile( "D:/logs/error.log", 300, "context3", "ssh tail" ) );

        sessionInfo.setOpenFiles( windowId, openFiles );

        WHEN( "Session is saved and restored" )
        {
            sessionInfo.saveToStorage( temp.settings() );

            SessionInfo restored;
            restored.retrieveFromStorage( temp.settings() );

            THEN( "Recorder command names are preserved" )
            {
                const auto& windows = restored.windows();
                REQUIRE( !windows.isEmpty() );

                const auto files = restored.openFiles( windowId );
                REQUIRE( files.size() == 3 );
                REQUIRE( files[ 0 ].recorderCommandName == "adb logcat" );
                REQUIRE( files[ 1 ].recorderCommandName.isEmpty() );
                REQUIRE( files[ 2 ].recorderCommandName == "ssh tail" );
            }
        }
    }
}
