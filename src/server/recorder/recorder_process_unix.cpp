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

#include "recorder_process.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace klogg::server {

namespace {

std::string readTail( const std::filesystem::path& path )
{
    std::ifstream input( path, std::ios::binary );
    if ( !input ) {
        return {};
    }
    input.seekg( 0, std::ios::end );
    const auto size = static_cast<std::streamoff>( input.tellg() );
    const auto start = std::max<std::streamoff>( 0, size - 8192 );
    input.seekg( start, std::ios::beg );
    std::string result;
    result.assign( std::istreambuf_iterator<char>( input ), std::istreambuf_iterator<char>() );
    return result;
}

std::filesystem::path makeErrorFile()
{
    std::error_code error;
    auto directory = std::filesystem::temp_directory_path( error );
    if ( error ) {
        directory = "/tmp";
    }
    return directory / ( "klogg-recorder-" + std::to_string( getpid() ) + "-"
                         + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count() )
                         + ".log" );
}

class PosixRecorderProcess final : public RecorderProcess {
public:
    PosixRecorderProcess( pid_t pid, std::filesystem::path errorFile )
        : pid_( pid )
        , errorFile_( std::move( errorFile ) )
    {
    }

    ~PosixRecorderProcess() override
    {
        const auto pid = pid_.load();
        if ( pid > 0 ) {
            terminate();
            int status = 0;
            waitpid( pid, &status, 0 );
            pid_ = -1;
        }
        std::error_code error;
        std::filesystem::remove( errorFile_, error );
    }

    int wait() override
    {
        const auto pid = pid_.exchange( -1 );
        if ( pid <= 0 ) {
            return -1;
        }
        int status = 0;
        if ( waitpid( pid, &status, 0 ) < 0 ) {
            return -1;
        }
        if ( WIFEXITED( status ) ) {
            return WEXITSTATUS( status );
        }
        return -1;
    }

    void terminate() override
    {
        const auto pid = pid_.load();
        if ( pid <= 0 ) {
            return;
        }
        kill( -pid, SIGTERM );
        std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
        kill( -pid, SIGKILL );
    }

    std::string errorOutput() const override
    {
        return readTail( errorFile_ );
    }

    int pid() const override
    {
        return pid_.load();
    }

private:
    std::atomic<pid_t> pid_ = -1;
    std::filesystem::path errorFile_;
};

} // namespace

std::unique_ptr<RecorderProcess> startRecorderProcess( const std::string& command,
                                                       const std::string& workingDirectory,
                                                       std::string& error )
{
    if ( command.empty() ) {
        error = "recorder command is empty";
        return nullptr;
    }

    const auto errorPath = makeErrorFile();
    const auto errorPathText = errorPath.string();
    const int errorFd = open( errorPathText.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600 );
    if ( errorFd < 0 ) {
        error = "creating recorder output file failed";
        return nullptr;
    }

    const auto pid = fork();
    if ( pid < 0 ) {
        close( errorFd );
        error = "fork failed";
        std::error_code removeError;
        std::filesystem::remove( errorPath, removeError );
        return nullptr;
    }

    if ( pid == 0 ) {
        setsid();
        const int inputFd = open( "/dev/null", O_RDONLY );
        if ( inputFd >= 0 ) {
            dup2( inputFd, STDIN_FILENO );
            close( inputFd );
        }
        dup2( errorFd, STDOUT_FILENO );
        dup2( errorFd, STDERR_FILENO );
        close( errorFd );

        if ( !workingDirectory.empty() && chdir( workingDirectory.c_str() ) != 0 ) {
            // The child reports through its exit status only; running the command
            // in the wrong directory would be silently wrong.
            std::fputs( "klogg_server: cannot change to the recorder working directory\n",
                        stderr );
            _exit( 126 );
        }

        execl( "/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>( nullptr ) );
        _exit( 127 );
    }

    close( errorFd );
    return std::make_unique<PosixRecorderProcess>( pid, errorPath );
}

} // namespace klogg::server
