#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace agentpulse {

// Thrown for any SQLite error.
class DbError : public std::runtime_error {
public:
    explicit DbError(const std::string& what) : std::runtime_error(what) {}
};

// A persisted job execution.
struct RunRecord {
    std::int64_t id = 0;
    std::string job_name;
    std::int64_t started_at = 0;  // unix seconds
    std::int64_t ended_at = 0;    // unix seconds
    std::string status;           // success/failed/timeout/spawn_error
    int exit_code = 0;
    long duration_ms = 0;
    std::string stdout_text;
    std::string stderr_text;
    std::string trigger;          // schedule/manual/missed
};

// A persisted alert transition.
struct AlertRecord {
    std::int64_t id = 0;
    std::int64_t ts = 0;
    std::string rule_name;
    std::string severity;
    std::string metric;
    std::string kind;        // firing/recovered
    double value = 0.0;
    double threshold = 0.0;
    std::string message;
    std::string attribution;
};

// Minimal RAII wrapper around a SQLite connection.
//
// Not thread-safe: SQLite connections must not be shared across threads
// without external synchronization. Each thread that needs the database
// should own its own Database, or callers must serialize access.
class Database {
public:
    // Opens (creating if needed) the database at `path`. Enables WAL so the
    // reader (socket thread) and writer (sampler thread) don't block each
    // other. Throws DbError on failure.
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;

    // Executes one or more statements with no result rows (DDL, PRAGMA).
    void exec(const std::string& sql);

    // Inserts a time-series metric sample.
    void insert_metric(std::int64_t ts_unix, const std::string& metric,
                       double value);

    // Returns the most recent value for `metric`, or nullopt if none.
    // The pair is {ts_unix, value}.
    std::optional<std::pair<std::int64_t, double>> latest_metric(
        const std::string& metric);

    // Inserts a completed job run; returns the new row id.
    std::int64_t insert_run(const RunRecord& run);

    // Returns the most recent run for `job_name`, or nullopt if none.
    std::optional<RunRecord> last_run(const std::string& job_name);

    // Returns the number of persisted runs for `job_name`.
    int count_runs(const std::string& job_name);

    // Inserts an alert transition; returns the new row id.
    std::int64_t insert_alert(const AlertRecord& alert);

    // Returns up to `limit` most-recent alerts, newest first.
    std::vector<AlertRecord> recent_alerts(int limit);

    sqlite3* handle() { return db_; }

private:
    sqlite3* db_ = nullptr;
};

// Applies the schema for the current milestone (idempotent).
void migrate(Database& db);

}  // namespace agentpulse
