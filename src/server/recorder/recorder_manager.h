#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "recorder_process.h"

namespace klogg::server {

struct RecorderStatus {
    std::uint64_t id = 0;
    std::string state; // running | finished | stopped | error
    int exitCode = 0;
    int pid = 0;
    std::string error;
};

// Server-owned registry of recorder commands. Command definitions and file
// bindings live in the Web UI; this manager only owns process lifetime.
class RecorderManager {
public:
    RecorderManager() = default;
    ~RecorderManager();

    RecorderManager( const RecorderManager& ) = delete;
    RecorderManager& operator=( const RecorderManager& ) = delete;

    std::optional<RecorderStatus> start( const std::string& command,
                                         const std::string& filePath,
                                         const std::string& owner, std::string& error );
    bool stop( std::uint64_t id, std::string& error );
    std::optional<RecorderStatus> status( std::uint64_t id ) const;
    void stopByOwner( const std::string& owner );
    void stopAll();

private:
    struct Entry {
        mutable std::mutex mutex;
        RecorderStatus status;
        std::string owner;
        bool stopRequested = false;
        std::shared_ptr<RecorderProcess> process;
        std::thread worker;
    };

    std::map<std::uint64_t, std::shared_ptr<Entry>> entries_;
    mutable std::mutex mutex_;
    std::uint64_t nextId_ = 1;
};

} // namespace klogg::server
