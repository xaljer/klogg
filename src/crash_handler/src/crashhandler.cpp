/*
 * Copyright (C) 2020, 2021 Anton Filimonov and other contributors
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

#include "crashhandler.h"

#include <cstdint>
#include <cstdlib>
#include <string_view>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

#ifdef KLOGG_USE_SENTRY
#include <qthreadpool.h>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSysInfo>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>

#ifdef KLOGG_USE_MIMALLOC
#include <mimalloc.h>
#endif

#include "client/crash_report_database.h"
#include "sentry.h"
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "cpu_info.h"
#include "issuereporter.h"
#include "klogg_version.h"
#include "log.h"
#include "memory_info.h"
#include "openfilehelper.h"

#ifdef KLOGG_USE_SENTRY
namespace {

constexpr const char* DSN
    = "https://aad3b270e5ba4ec2915eb5caf6e6d929@o453796.ingest.sentry.io/5442855";

QString sentryDatabasePath()
{
#ifdef KLOGG_PORTABLE
    auto basePath = QCoreApplication::applicationDirPath();
#else
    auto basePath = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
#endif

    return basePath.append( "/klogg_dump" );
}

void logSentry( sentry_level_t level, const char* message, va_list args, void* userdata )
{
#if defined __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif

    Q_UNUSED( userdata );
    QString formattedMessage;
    switch ( level ) {
    case SENTRY_LEVEL_WARNING:
        qWarning( message, args );
        break;
    case SENTRY_LEVEL_ERROR:
        qCritical( message, args );
        break;
    default:
        qInfo( message, args );
        break;
    }

#if defined __clang__
#pragma clang diagnostic pop
#elif defined __GNUC__
#pragma GCC diagnostic pop
#endif
}

QDialog::DialogCode askUserConfirmation( const QString& formattedReport, const QString& reportPath )
{
    auto message = std::make_unique<QLabel>();
    message->setText( "During last run application has encountered an unexpected error." );

    auto crashReportHeader = std::make_unique<QLabel>();
    crashReportHeader->setText( "We collected the following crash report:" );

    auto report = std::make_unique<QPlainTextEdit>();
    report->setReadOnly( true );
    report->setPlainText( formattedReport );
    report->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );

    auto sendReportLabel = std::make_unique<QLabel>();
    sendReportLabel->setText( "Application can send this report to sentry.io for developers to "
                              "analyze and fix the issue" );

    auto privacyPolicy = std::make_unique<QLabel>();
    privacyPolicy->setText(
        "<a href=\"https://klogg.filimonov.dev/docs/privacy_policy\">Privacy policy</a>" );

    privacyPolicy->setTextFormat( Qt::RichText );
    privacyPolicy->setTextInteractionFlags( Qt::TextBrowserInteraction );
    privacyPolicy->setOpenExternalLinks( true );

    auto exploreButton = std::make_unique<QPushButton>();
    exploreButton->setText( "Open report directory" );
    exploreButton->setFlat( true );
    QObject::connect( exploreButton.get(), &QPushButton::clicked,
                      [ &reportPath ] { showPathInFileExplorer( reportPath ); } );

    auto privacyLayout = std::make_unique<QHBoxLayout>();
    privacyLayout->addWidget( privacyPolicy.release() );
    privacyLayout->addStretch();
    privacyLayout->addWidget( exploreButton.release() );

    auto buttonBox = std::make_unique<QDialogButtonBox>();
    buttonBox->addButton( "Send report", QDialogButtonBox::AcceptRole );
    buttonBox->addButton( "Discard report", QDialogButtonBox::RejectRole );

    auto confirmationDialog = std::make_unique<QDialog>();
    confirmationDialog->resize( 800, 600 );

    QObject::connect( buttonBox.get(), &QDialogButtonBox::accepted, confirmationDialog.get(),
                      &QDialog::accept );
    QObject::connect( buttonBox.get(), &QDialogButtonBox::rejected, confirmationDialog.get(),
                      &QDialog::reject );

    auto layout = std::make_unique<QVBoxLayout>();
    layout->addWidget( message.release() );
    layout->addWidget( crashReportHeader.release() );
    layout->addWidget( report.release() );
    layout->addWidget( sendReportLabel.release() );
    layout->addLayout( privacyLayout.release() );
    layout->addWidget( buttonBox.release() );

    confirmationDialog->setLayout( layout.release() );

    return static_cast<QDialog::DialogCode>( confirmationDialog->exec() );
}

bool checkCrashpadReports( const QString& databasePath )
{
    using namespace crashpad;

    bool needWaitForUpload = false;

#ifdef Q_OS_WIN
    auto database = CrashReportDatabase::InitializeWithoutCreating(
        base::FilePath{ databasePath.toStdWString() } );
#else
    auto database = CrashReportDatabase::InitializeWithoutCreating(
        base::FilePath{ databasePath.toStdString() } );
#endif

    std::vector<CrashReportDatabase::Report> pendingReports;
    database->GetCompletedReports( &pendingReports );
    LOG_INFO << "Pending reports " << pendingReports.size();

#ifdef Q_OS_WIN
    const auto stackwalker = QCoreApplication::applicationDirPath() + "/klogg_minidump_dump.exe";
#else
    const auto stackwalker = QCoreApplication::applicationDirPath() + "/klogg_minidump_dump";
#endif

    for ( const auto& report : pendingReports ) {
        if ( report.uploaded ) {
            continue;
        }

#ifdef Q_OS_WIN
        const auto reportFile = QString::fromStdWString( report.file_path.value() );
#else
        const auto reportFile = QString::fromStdString( report.file_path.value() );
#endif

        QProcess stackProcess;
        stackProcess.start( stackwalker, QStringList() << reportFile );
        stackProcess.waitForFinished();

        QString formattedReport = reportFile;
        formattedReport.append( QChar::LineFeed )
            .append( QString::fromUtf8( stackProcess.readAllStandardOutput() ) );

        if ( QDialog::Accepted == askUserConfirmation( formattedReport, reportFile ) ) {
            database->RequestUpload( report.uuid );
            needWaitForUpload = true;
        }
        else {
            database->DeleteReport( report.uuid );
        }

        IssueReporter::askUserAndReportIssue( IssueTemplate::Crash,
                                              report.uuid.ToString().c_str() );
    }
    return needWaitForUpload;
}
} // namespace

CrashHandler::CrashHandler( const QString& dumpDirectory )
{
    (void)dumpDirectory;

    LOG_INFO << "CrashHandler initializing";

    const auto dumpPath = sentryDatabasePath();
    const auto hasDumpDir = QDir{ dumpPath }.mkpath( "." );

    LOG_INFO << "Crashpad database path " << dumpPath << ", hasDumpDir " << hasDumpDir;

    const auto needWaitForUpload = hasDumpDir ? checkCrashpadReports( dumpPath ) : false;

    LOG_INFO << "Crashpad reports checked, needWaitForUpload " << needWaitForUpload;

    sentry_options_t* sentryOptions = sentry_options_new();

    sentry_options_set_logger( sentryOptions, logSentry, nullptr );
    sentry_options_set_debug( sentryOptions, 1 );

#ifdef Q_OS_WIN
    const auto handlerPath = QCoreApplication::applicationDirPath() + "/klogg_crashpad_handler.exe";
    sentry_options_set_database_pathw( sentryOptions, dumpPath.toStdWString().c_str() );
    sentry_options_set_handler_pathw( sentryOptions, handlerPath.toStdWString().c_str() );
#else
    const auto handlerPath = QCoreApplication::applicationDirPath() + "/klogg_crashpad_handler";
    sentry_options_set_database_path( sentryOptions, dumpPath.toStdString().c_str() );
    sentry_options_set_handler_path( sentryOptions, handlerPath.toStdString().c_str() );
#endif

    sentry_options_set_dsn( sentryOptions, DSN );

    // klogg asks confirmation and sends reports using crashpad
    sentry_options_set_require_user_consent( sentryOptions, true );

    sentry_options_set_auto_session_tracking( sentryOptions, false );

    sentry_options_set_symbolize_stacktraces( sentryOptions, true );

    sentry_options_set_environment( sentryOptions, "development" );
    sentry_options_set_release( sentryOptions, kloggVersion().data() );

    sentry_init( sentryOptions );

    LOG_INFO << "Sentry initialized";

    sentry_set_tag( "commit", kloggCommit().data() );
    sentry_set_tag( "qt", qVersion() );
    sentry_set_tag( "build_arch", QSysInfo::buildCpuArchitecture().toLatin1().data() );

    auto addExtra = []( const char* name, auto value ) {
        sentry_set_extra( name, sentry_value_new_string( std::to_string( value ).c_str() ) );
        LOG_INFO << "Process stats: " << name << " - " << value;
    };

    addExtra( "memory", physicalMemory() );

    addExtra( "cpuInstructions", static_cast<unsigned>( supportedCpuInstructions() ) );
    addExtra( "concurrency", QThreadPool::globalInstance()->maxThreadCount() );

    memoryUsageTimer_ = std::make_unique<QTimer>();
    QObject::connect( memoryUsageTimer_.get(), &QTimer::timeout, [ addExtra ]() {
        const auto vmUsed = usedMemory();
        addExtra( "vm_used", vmUsed );

#ifdef KLOGG_USE_MIMALLOC
        size_t elapsedMsecs, userMsecs, systemMsecs, currentRss, peakRss, currentCommit, peakCommit,
            pageFaults;

        mi_process_info( &elapsedMsecs, &userMsecs, &systemMsecs, &currentRss, &peakRss,
                         &currentCommit, &peakCommit, &pageFaults );

        addExtra( "elapsed_msecs", elapsedMsecs );
        addExtra( "user_msecs", userMsecs );
        addExtra( "system_msecs", systemMsecs );
        addExtra( "current_rss", currentRss );
        addExtra( "peak_rss", peakRss );
        addExtra( "current_commit", currentCommit );
        addExtra( "peak_commit", peakCommit );
        addExtra( "page_faults", pageFaults );
#endif
    } );
    memoryUsageTimer_->start( 10000 );

    if ( needWaitForUpload ) {
        QProgressDialog progressDialog;
        progressDialog.setLabelText( "Uploading crash reports" );
        progressDialog.setRange( 0, 0 );

        QTimer::singleShot( 30 * 1000, &progressDialog, &QProgressDialog::cancel );
        progressDialog.exec();
    }
}

CrashHandler::~CrashHandler()
{
    memoryUsageTimer_->stop();
    sentry_close();
}
#elif defined( Q_OS_WIN )
namespace {

wchar_t g_dumpDirectory[ MAX_PATH ] = { 0 };

const wchar_t* exceptionCodeToString( DWORD code )
{
    switch ( code ) {
    case EXCEPTION_ACCESS_VIOLATION:
        return L"ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return L"ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return L"DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        return L"FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION:
        return L"FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:
        return L"FLT_OVERFLOW";
    case EXCEPTION_FLT_UNDERFLOW:
        return L"FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return L"ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return L"INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:
        return L"INT_OVERFLOW";
    case EXCEPTION_PRIV_INSTRUCTION:
        return L"PRIV_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW:
        return L"STACK_OVERFLOW";
    default:
        return L"UNKNOWN";
    }
}

void writeLineToFile( HANDLE hFile, const wchar_t* line )
{
    DWORD written;
    const auto len = static_cast<DWORD>( wcslen( line ) * sizeof( wchar_t ) );
    WriteFile( hFile, line, len, &written, nullptr );
    const wchar_t nl = L'\n';
    WriteFile( hFile, &nl, sizeof( wchar_t ), &written, nullptr );
}

LONG WINAPI unhandledExceptionFilter( PEXCEPTION_POINTERS exceptionInfo )
{
    if ( g_dumpDirectory[ 0 ] == 0 ) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    wchar_t tracePath[ MAX_PATH ];
    SYSTEMTIME st;
    GetLocalTime( &st );
    wsprintfW( tracePath,
               L"%s\\klogg_backtrace_%04d-%02d-%02d_%02d-%02d-%02d.txt",
               g_dumpDirectory, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );

    HANDLE hFile = CreateFileW( tracePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( hFile == INVALID_HANDLE_VALUE ) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    wchar_t line[ 512 ];

    writeLineToFile( hFile, L"Klogg crash report" );

    wsprintfW( line, L"Exception code: 0x%08lX (%s)", exceptionInfo->ExceptionRecord->ExceptionCode,
               exceptionCodeToString( exceptionInfo->ExceptionRecord->ExceptionCode ) );
    writeLineToFile( hFile, line );

    wsprintfW( line, L"Fault address: 0x%p", exceptionInfo->ExceptionRecord->ExceptionAddress );
    writeLineToFile( hFile, line );

    writeLineToFile( hFile, L"" );
    writeLineToFile( hFile, L"Stack trace:" );

    constexpr int kMaxFrames = 62;
    void* frames[ kMaxFrames ];
    const USHORT frameCount = RtlCaptureStackBackTrace( 0, kMaxFrames, frames, nullptr );

    for ( USHORT i = 0; i < frameCount; ++i ) {
        HMODULE module = nullptr;
        wchar_t moduleName[ MAX_PATH ] = { 0 };

        MEMORY_BASIC_INFORMATION mbi;
        if ( VirtualQuery( frames[ i ], &mbi, sizeof( mbi ) )
             && mbi.Type == MEM_IMAGE ) {
            module = static_cast<HMODULE>( mbi.AllocationBase );
        }

        const wchar_t* moduleNamePtr = L"<unknown>";
        if ( module && GetModuleFileNameW( module, moduleName, MAX_PATH ) ) {
            const auto* fileName = wcsrchr( moduleName, L'\\' );
            moduleNamePtr = fileName ? ( fileName + 1 ) : moduleName;
        }

        const auto offset = reinterpret_cast<uintptr_t>( frames[ i ] )
                            - reinterpret_cast<uintptr_t>( module );

        wsprintfW( line, L"  #%u: %s+0x%llX", i, moduleNamePtr,
                   static_cast<unsigned long long>( offset ) );
        writeLineToFile( hFile, line );
    }

    CloseHandle( hFile );
    return EXCEPTION_EXECUTE_HANDLER;
}

void writeBacktraceFile( const wchar_t* reason )
{
    if ( g_dumpDirectory[ 0 ] == 0 ) {
        return;
    }

    wchar_t tracePath[ MAX_PATH ];
    SYSTEMTIME st;
    GetLocalTime( &st );
    wsprintfW( tracePath,
               L"%s\\klogg_backtrace_%04d-%02d-%02d_%02d-%02d-%02d.txt",
               g_dumpDirectory, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );

    HANDLE hFile = CreateFileW( tracePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( hFile == INVALID_HANDLE_VALUE ) {
        return;
    }

    wchar_t line[ 512 ];

    writeLineToFile( hFile, L"Klogg crash report" );
    wsprintfW( line, L"Termination reason: %s", reason );
    writeLineToFile( hFile, line );
    writeLineToFile( hFile, L"" );
    writeLineToFile( hFile, L"Stack trace:" );

    constexpr int kMaxFrames = 62;
    void* frames[ kMaxFrames ];
    const USHORT frameCount = RtlCaptureStackBackTrace( 0, kMaxFrames, frames, nullptr );

    for ( USHORT i = 0; i < frameCount; ++i ) {
        HMODULE module = nullptr;
        wchar_t moduleName[ MAX_PATH ] = { 0 };

        MEMORY_BASIC_INFORMATION mbi;
        if ( VirtualQuery( frames[ i ], &mbi, sizeof( mbi ) )
             && mbi.Type == MEM_IMAGE ) {
            module = static_cast<HMODULE>( mbi.AllocationBase );
        }

        const wchar_t* moduleNamePtr = L"<unknown>";
        if ( module && GetModuleFileNameW( module, moduleName, MAX_PATH ) ) {
            const auto* fileName = wcsrchr( moduleName, L'\\' );
            moduleNamePtr = fileName ? ( fileName + 1 ) : moduleName;
        }

        const auto offset = reinterpret_cast<uintptr_t>( frames[ i ] )
                            - reinterpret_cast<uintptr_t>( module );

        wsprintfW( line, L"  #%u: %s+0x%llX", i, moduleNamePtr,
                   static_cast<unsigned long long>( offset ) );
        writeLineToFile( hFile, line );
    }

    CloseHandle( hFile );
}

void terminateHandler()
{
    writeBacktraceFile( L"std::terminate" );
    abort();
}

void __cdecl purecallHandler()
{
    writeBacktraceFile( L"pure virtual function call" );
    abort();
}

void __cdecl invalidParameterHandler( const wchar_t*, const wchar_t*, const wchar_t*,
                                      unsigned int, uintptr_t )
{
    writeBacktraceFile( L"CRT invalid parameter" );
    abort();
}

} // namespace

CrashHandler::CrashHandler( const QString& dumpDirectory )
{
    const auto dir = dumpDirectory.isEmpty()
                         ? QStandardPaths::writableLocation( QStandardPaths::TempLocation )
                               + QDir::separator() + "klogg"
                         : dumpDirectory;
    QDir{ dir }.mkpath( "." );
    wcsncpy_s( g_dumpDirectory, dir.toStdWString().c_str(), MAX_PATH - 1 );

    SetUnhandledExceptionFilter( unhandledExceptionFilter );
    std::set_terminate( terminateHandler );
    _set_purecall_handler( purecallHandler );
    _set_invalid_parameter_handler( invalidParameterHandler );
}

CrashHandler::~CrashHandler()
{
    SetUnhandledExceptionFilter( nullptr );
}
#else
CrashHandler::CrashHandler( const QString& )
{
}

CrashHandler::~CrashHandler()
{
}
#endif
