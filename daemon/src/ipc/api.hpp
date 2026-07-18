#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace agentpulse {

class SharedState;
class Database;

// Builds the JSON response line for a request line received on the socket.
class Api {
public:
    // Invoked to trigger an immediate job run; returns false if unknown job.
    using RunHandler = std::function<bool(const std::string&)>;

    // `db_path`, if non-empty, opens a dedicated read connection used for
    // history/runs queries (safe: SQLite WAL allows concurrent readers, and
    // the socket thread is the only caller of handle()).
    Api(const SharedState& state, std::int64_t started_at,
        const std::string& db_path = "");
    ~Api();

    void set_run_handler(RunHandler handler) { run_handler_ = std::move(handler); }

    // request: a single line (no trailing newline). Returns a JSON response.
    std::string handle(const std::string& request);

private:
    const SharedState& state_;
    std::int64_t started_at_;
    RunHandler run_handler_;
    std::unique_ptr<Database> db_;  // read connection for history/runs
};

}  // namespace agentpulse
