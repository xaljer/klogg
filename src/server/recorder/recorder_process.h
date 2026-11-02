#pragma once

#include <memory>
#include <string>

namespace klogg::server {

// One shell command started by the recorder. The platform implementations keep
// process handles and a small stderr capture file; wait() is called from the
// recorder manager's worker thread.
class RecorderProcess {
public:
    virtual ~RecorderProcess() = default;

    // Blocks until the process exits and returns its exit code (-1 if the
    // process could not be reaped or was terminated by a signal).
    virtual int wait() = 0;

    // Terminates the process and its children. Safe to call while wait() is
    // blocked on another thread.
    virtual void terminate() = 0;

    // Text captured from stdout/stderr, used for error reporting.
    virtual std::string errorOutput() const = 0;

    virtual int pid() const = 0;
};

std::unique_ptr<RecorderProcess> startRecorderProcess( const std::string& command,
                                                       const std::string& workingDirectory,
                                                       std::string& error );

} // namespace klogg::server
