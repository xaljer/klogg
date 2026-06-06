#include "recorder.h"
#include "shellprocess.h"

#include <QDir>

#include "log.h"

RecorderManager::RecorderManager( QObject* parent )
    : QObject( parent )
{
}

RecorderManager::~RecorderManager()
{
    if ( process_ ) {
        process_->kill();
        process_->waitForFinished( 3000 );
        delete process_;
    }
}

void RecorderManager::start( const QString& filePath, const QString& command )
{
    if ( state_ != State::Idle ) {
        return;
    }

    auto resolvedCommand = command;
    resolvedCommand.replace( QString( "{file}" ), QDir::toNativeSeparators( filePath ) );

    LOG_INFO << "Recorder:" << resolvedCommand;

    process_ = new QProcess( this );
    ShellProcess::setup( process_, resolvedCommand );

    connect( process_,
             QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ), this,
             &RecorderManager::onProcessFinished );

    connect( process_, &QProcess::errorOccurred, this, &RecorderManager::onProcessError );

    state_ = State::Recording;
    Q_EMIT recordingStarted();
}

void RecorderManager::stop()
{
    if ( state_ != State::Recording ) {
        return;
    }

    state_ = State::Stopping;
    ShellProcess::stop( process_ );
}

void RecorderManager::onProcessFinished( int exitCode, QProcess::ExitStatus status )
{
    if ( !process_ ) {
        return;
    }

    const auto stdoutOutput
        = QString::fromLocal8Bit( process_->readAllStandardOutput() ).trimmed();

    LOG_INFO << "Recorder finished, exitCode=" << exitCode
             << " status=" << static_cast<int>( status );
    if ( !stdoutOutput.isEmpty() ) {
        LOG_INFO << "Recorder output:" << stdoutOutput;
    }

    lastExitCode_ = exitCode;
    lastExitStatus_ = status;

    switch ( state_ ) {
    case State::Recording:
        if ( exitCode != 0 || status == QProcess::CrashExit ) {
            lastError_ = status == QProcess::CrashExit
                             ? tr( "Process crashed" )
                             : tr( "Process exited with code %1" ).arg( exitCode );
            if ( !stdoutOutput.isEmpty() ) {
                lastError_ += QString( "\n%1" ).arg( stdoutOutput );
            }
            transitionTo( State::Error );
        }
        else {
            transitionTo( State::Finished );
        }
        break;
    case State::Stopping:
        transitionTo( State::Stopped );
        break;
    default:
        break;
    }
}

void RecorderManager::onProcessError()
{
    lastExitCode_ = -1;
    lastError_ = process_ ? process_->errorString() : tr( "Unknown error" );
    if ( lastError_.isEmpty() ) {
        lastError_ = tr( "Process failed to start" );
    }
    transitionTo( State::Error );
}

void RecorderManager::transitionTo( State next )
{
    state_ = next;

    switch ( state_ ) {
    case State::Error:
        Q_EMIT recordingError( lastError_ );
        Q_EMIT recordingStopped();
        break;
    case State::Finished:
        Q_EMIT recordingStopped();
        break;
    case State::Stopped:
        Q_EMIT recordingStopped();
        break;
    default:
        return; // transient states don't emit
    }

    // Clean up and go back to idle
    if ( process_ ) {
        delete process_;
        process_ = nullptr;
    }
    state_ = State::Idle;
}
