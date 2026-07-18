#include "db.hpp"

#include <sqlite3.h>

namespace agentpulse {

namespace {

// Runs a statement that yields no rows and throws on error.
void exec_or_throw(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw DbError(msg);
    }
}

}  // namespace

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        db_ = nullptr;
        throw DbError("cannot open database: " + msg);
    }
    // WAL lets the socket reader and sampler writer proceed concurrently;
    // busy_timeout avoids spurious SQLITE_BUSY under contention.
    exec_or_throw(db_, "PRAGMA journal_mode=WAL;");
    exec_or_throw(db_, "PRAGMA synchronous=NORMAL;");
    exec_or_throw(db_, "PRAGMA foreign_keys=ON;");
    sqlite3_busy_timeout(db_, 2000);
}

Database::~Database() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

Database::Database(Database&& other) noexcept : db_(other.db_) {
    other.db_ = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        if (db_ != nullptr) {
            sqlite3_close(db_);
        }
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

void Database::exec(const std::string& sql) {
    exec_or_throw(db_, sql.c_str());
}

void Database::insert_metric(std::int64_t ts_unix, const std::string& metric,
                             double value) {
    static const char* kSql =
        "INSERT INTO metrics(ts, metric, value) VALUES(?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DbError(std::string("prepare insert_metric: ") +
                      sqlite3_errmsg(db_));
    }
    sqlite3_bind_int64(stmt, 1, ts_unix);
    sqlite3_bind_text(stmt, 2, metric.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, value);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw DbError(std::string("step insert_metric: ") +
                      sqlite3_errmsg(db_));
    }
}

std::optional<std::pair<std::int64_t, double>> Database::latest_metric(
    const std::string& metric) {
    static const char* kSql =
        "SELECT ts, value FROM metrics WHERE metric = ? "
        "ORDER BY ts DESC, id DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DbError(std::string("prepare latest_metric: ") +
                      sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, metric.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<std::pair<std::int64_t, double>> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = {sqlite3_column_int64(stmt, 0), sqlite3_column_double(stmt, 1)};
    }
    sqlite3_finalize(stmt);
    return result;
}

void migrate(Database& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS meta("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL);"

        "CREATE TABLE IF NOT EXISTS metrics("
        "  id     INTEGER PRIMARY KEY,"
        "  ts     INTEGER NOT NULL,"   // unix seconds
        "  metric TEXT    NOT NULL,"   // e.g. 'system.cpu.percent'
        "  value  REAL    NOT NULL);"

        "CREATE INDEX IF NOT EXISTS idx_metrics_metric_ts"
        "  ON metrics(metric, ts);");
}

}  // namespace agentpulse
