#ifndef KLOGG_RECORDER_H
#define KLOGG_RECORDER_H

#include <QObject>
#include <QProcess>

class RecorderManager : public QObject {
    Q_OBJECT

  public:
    enum class State {
        Idle,       // Not recording, ready to start
        Recording,  // Process is running
        Stopping,   // User requested stop, waiting for process exit
        Finished,   // Process exited normally (transient → Idle)
        Error,      // Process failed (transient → Idle)
        Stopped,    // User stop completed (transient → Idle)
    };

    explicit RecorderManager( QObject* parent = nullptr );
    ~RecorderManager() override;

    bool isRecording() const
    {
        return state_ == State::Recording || state_ == State::Stopping;
    }
    State state() const
    {
        return state_;
    }
    QString boundCommandName() const
    {
        return boundCommandName_;
    }
    void setBoundCommandName( const QString& name )
    {
        boundCommandName_ = name;
    }

  public Q_SLOTS:
    void start( const QString& filePath, const QString& command );
    void stop();

  Q_SIGNALS:
    void recordingStarted();
    void recordingStopped();
    void recordingError( const QString& error );

  private:
    void onProcessFinished( int exitCode, QProcess::ExitStatus status );
    void onProcessError();
    void transitionTo( State next );

    State state_ = State::Idle;
    QProcess* process_ = nullptr;
    QString boundCommandName_;
    QString lastError_;
    int lastExitCode_ = 0;
    QProcess::ExitStatus lastExitStatus_ = QProcess::NormalExit;
};

#endif
