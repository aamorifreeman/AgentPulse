#pragma once

#include <cstdint>
#include <string>

namespace agentpulse {

class SharedState;

// Builds the JSON response line for a request line received on the socket.
// `started_at` is the daemon's start time (unix seconds), used by "status".
class Api {
public:
    Api(const SharedState& state, std::int64_t started_at)
        : state_(state), started_at_(started_at) {}

    // request: a single line (no trailing newline), either a JSON object with
    // a "cmd" field or a bare command word. Returns a JSON response line.
    std::string handle(const std::string& request) const;

private:
    const SharedState& state_;
    std::int64_t started_at_;
};

}  // namespace agentpulse
