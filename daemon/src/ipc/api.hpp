#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace agentpulse {

class SharedState;

// Builds the JSON response line for a request line received on the socket.
class Api {
public:
    // Invoked to trigger an immediate job run; returns false if unknown job.
    using RunHandler = std::function<bool(const std::string&)>;

    Api(const SharedState& state, std::int64_t started_at)
        : state_(state), started_at_(started_at) {}

    // Sets the handler for the "run" command. If unset, "run" reports that
    // job control is unavailable.
    void set_run_handler(RunHandler handler) { run_handler_ = std::move(handler); }

    // request: a single line (no trailing newline), either a JSON object with
    // a "cmd" field (and optional "job") or a bare "cmd [arg]". Returns a JSON
    // response line.
    std::string handle(const std::string& request) const;

private:
    const SharedState& state_;
    std::int64_t started_at_;
    RunHandler run_handler_;
};

}  // namespace agentpulse
