#pragma once

#include <cstdint>
#include <string>

namespace agentpulse {

enum class RunStatus {
    Success,     // exited 0
    Failed,      // exited non-zero or killed by a signal
    Timeout,     // exceeded timeout_seconds and was killed
    SpawnError,  // could not be launched
};

std::string to_string(RunStatus status);

struct RunResult {
    RunStatus status = RunStatus::SpawnError;
    int exit_code = -1;             // process exit code, or -signal if signalled
    std::int64_t started_at = 0;    // unix seconds
    std::int64_t ended_at = 0;      // unix seconds
    long duration_ms = 0;
    std::string stdout_text;
    std::string stderr_text;
    bool stdout_truncated = false;
    bool stderr_truncated = false;
    std::string error;              // populated for SpawnError
};

// Runs `command` via `/bin/sh -c`, capturing stdout and stderr (each capped;
// truncation is flagged). If `timeout_seconds > 0` and the command runs
// longer, its process group is killed and the status is Timeout. Blocks until
// the command finishes.
RunResult run_command(const std::string& command, int timeout_seconds);

}  // namespace agentpulse
