#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace klogg::server {

struct RecorderCommandConfig {
    std::string name;
    std::string command;
};

// Recorder definitions are user configuration (config.toml); per-file
// bindings are transient session state (session.toml), matching the desktop
// Recorder semantics.
class RecorderConfig {
public:
    std::vector<RecorderCommandConfig> commands() const;
    std::optional<std::string> commandForName( const std::string& name ) const;
    bool replaceCommands( const std::vector<RecorderCommandConfig>& commands, std::string& error );

    std::vector<std::pair<std::string, std::string>> bindings() const;
    std::string bindingForFile( const std::string& filePath ) const;
    bool setBinding( const std::string& filePath, const std::string& commandName,
                     std::string& error );

private:
    mutable std::recursive_mutex mutex_;
};

} // namespace klogg::server
