#pragma once

#include <filesystem>

namespace agentpulse {

// Resolves and (on first call) creates the per-user directories AgentPulse
// uses. All paths live under the invoking user's home directory so the
// daemon can run as a per-user launchd LaunchAgent with no elevated rights.
struct Paths {
    std::filesystem::path data_dir;    // ~/Library/Application Support/AgentPulse
    std::filesystem::path config_dir;  // ~/.config/agentpulse
    std::filesystem::path log_dir;     // ~/Library/Logs/AgentPulse

    std::filesystem::path database() const;    // data_dir / agentpulse.db
    std::filesystem::path socket() const;      // data_dir / agentpulse.sock
    std::filesystem::path config_file() const; // config_dir / config.yaml
};

// Computes the standard paths and ensures the directories exist.
// Throws std::runtime_error if $HOME is unset or a directory can't be created.
Paths resolve_paths();

}  // namespace agentpulse
