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

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

namespace klogg::server {

namespace {

std::wstring widen( const std::string& text )
{
    if ( text.empty() ) {
        return {};
    }
    const auto size = MultiByteToWideChar( CP_UTF8, 0, text.data(),
                                           static_cast<int>( text.size() ), nullptr, 0 );
    std::wstring result( static_cast<std::size_t>( size ), L'\0' );
    MultiByteToWideChar( CP_UTF8, 0, text.data(), static_cast<int>( text.size() ), result.data(),
                         size );
    return result;
}

std::string lastError( const char* operation )
{
    return std::string( operation ) + " failed (Windows error "
           + std::to_string( GetLastError() ) + ")";
}

std::filesystem::path makeErrorFile()
{
    std::error_code error;
    auto directory = std::filesystem::temp_directory_path( error );
    if ( error ) {
        directory = L".";
    }
    const auto suffix = std::to_string( GetCurrentProcessId() ) + "-"
                        + std::to_string( GetTickCount64() );
    return directory / ( "klogg-recorder-" + suffix + ".log" );
}

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
    if ( result.empty() ) {
        return {};
    }

    // Console programs on Windows usually emit the OEM code page. Convert it
    // before putting the text into a JSON response; otherwise invalid UTF-8
    // would corrupt the whole RPC envelope.
    if ( MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, result.data(),
                              static_cast<int>( result.size() ), nullptr, 0 )
         > 0 ) {
        return result;
    }
    const auto wideSize = MultiByteToWideChar( CP_ACP, 0, result.data(),
                                               static_cast<int>( result.size() ), nullptr, 0 );
    if ( wideSize <= 0 ) {
        return {};
    }
    std::wstring wide( static_cast<std::size_t>( wideSize ), L'\0' );
    MultiByteToWideChar( CP_ACP, 0, result.data(), static_cast<int>( result.size() ),
                         wide.data(), wideSize );
    const auto utf8Size = WideCharToMultiByte( CP_UTF8, 0, wide.data(), wideSize, nullptr, 0,
                                               nullptr, nullptr );
    if ( utf8Size <= 0 ) {
        return {};
    }
    std::string utf8( static_cast<std::size_t>( utf8Size ), '\0' );
    WideCharToMultiByte( CP_UTF8, 0, wide.data(), wideSize, utf8.data(), utf8Size, nullptr,
                         nullptr );
    return utf8;
}

class WindowsRecorderProcess final : public RecorderProcess {
public:
    WindowsRecorderProcess( HANDLE process, HANDLE job, int pid, std::filesystem::path errorFile )
        : process_( process )
        , job_( job )
        , pid_( pid )
        , errorFile_( std::move( errorFile ) )
    {
    }

    ~WindowsRecorderProcess() override
    {
        if ( process_ ) {
            terminate();
            WaitForSingleObject( process_, 5000 );
            CloseHandle( process_ );
            process_ = nullptr;
        }
        if ( job_ ) {
            CloseHandle( job_ );
            job_ = nullptr;
        }
        std::error_code error;
        std::filesystem::remove( errorFile_, error );
    }

    int wait() override
    {
        if ( !process_ ) {
            return -1;
        }
        const auto waitResult = WaitForSingleObject( process_, INFINITE );
        DWORD exitCode = 0;
        if ( waitResult != WAIT_OBJECT_0 || !GetExitCodeProcess( process_, &exitCode ) ) {
            exitCode = static_cast<DWORD>( -1 );
        }
        CloseHandle( process_ );
        process_ = nullptr;
        if ( job_ ) {
            CloseHandle( job_ );
            job_ = nullptr;
        }
        return static_cast<int>( exitCode );
    }

    void terminate() override
    {
        if ( job_ ) {
            TerminateJobObject( job_, 1 );
        }
        else if ( process_ ) {
            TerminateProcess( process_, 1 );
        }
    }

    std::string errorOutput() const override
    {
        return readTail( errorFile_ );
    }

    int pid() const override
    {
        return pid_;
    }

private:
    HANDLE process_ = nullptr;
    HANDLE job_ = nullptr;
    int pid_ = 0;
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
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof( security );
    security.bInheritHandle = TRUE;

    const auto errorPathWide = errorPath.wstring();
    const HANDLE errorHandle
        = CreateFileW( errorPathWide.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( errorHandle == INVALID_HANDLE_VALUE ) {
        error = lastError( "creating recorder output file" );
        return nullptr;
    }

    const HANDLE inputHandle
        = CreateFileW( L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( inputHandle == INVALID_HANDLE_VALUE ) {
        CloseHandle( errorHandle );
        error = lastError( "opening recorder stdin" );
        return nullptr;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof( startup );
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = inputHandle;
    startup.hStdOutput = errorHandle;
    startup.hStdError = errorHandle;

    std::wstring commandLine = L"cmd.exe /d /s /c \"" + widen( command ) + L"\"";
    const auto commandWide = widen( workingDirectory );
    PROCESS_INFORMATION processInfo{};
    const BOOL started = CreateProcessW( nullptr, commandLine.data(), &security, &security, TRUE,
                                         CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                         commandWide.empty() ? nullptr : commandWide.c_str(),
                                         &startup, &processInfo );
    CloseHandle( inputHandle );
    CloseHandle( errorHandle );

    if ( !started ) {
        error = lastError( "starting recorder command" );
        std::error_code removeError;
        std::filesystem::remove( errorPath, removeError );
        return nullptr;
    }

    HANDLE job = CreateJobObjectW( nullptr, nullptr );
    bool jobAssigned = false;
    if ( job ) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject( job, JobObjectExtendedLimitInformation, &limits, sizeof( limits ) );
        jobAssigned = AssignProcessToJobObject( job, processInfo.hProcess ) != FALSE;
    }

    ResumeThread( processInfo.hThread );
    CloseHandle( processInfo.hThread );

    if ( !jobAssigned && job ) {
        CloseHandle( job );
        job = nullptr;
    }

    return std::make_unique<WindowsRecorderProcess>(
        processInfo.hProcess, job, static_cast<int>( processInfo.dwProcessId ), errorPath );
}

} // namespace klogg::server
