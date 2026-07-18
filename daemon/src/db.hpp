#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

struct sqlite3;
struct sqlite3_stmt;

namespace agentpulse {

// Thrown for any SQLite error.
class DbError : public std::runtime_error {
public:
    explicit DbError(const std::string& what) : std::runtime_error(what) {}
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

    sqlite3* handle() { return db_; }

private:
    sqlite3* db_ = nullptr;
};

// Applies the schema for the current milestone (idempotent).
void migrate(Database& db);

}  // namespace agentpulse
