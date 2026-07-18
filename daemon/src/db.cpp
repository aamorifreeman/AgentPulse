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

std::int64_t Database::insert_run(const RunRecord& run) {
    static const char* kSql =
        "INSERT INTO runs(job_name, started_at, ended_at, status, exit_code,"
        "                 duration_ms, stdout, stderr, trigger)"
        " VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DbError(std::string("prepare insert_run: ") +
                      sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, run.job_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, run.started_at);
    sqlite3_bind_int64(stmt, 3, run.ended_at);
    sqlite3_bind_text(stmt, 4, run.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, run.exit_code);
    sqlite3_bind_int64(stmt, 6, run.duration_ms);
    sqlite3_bind_text(stmt, 7, run.stdout_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, run.stderr_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, run.trigger.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw DbError(std::string("step insert_run: ") + sqlite3_errmsg(db_));
    }
    return sqlite3_last_insert_rowid(db_);
}

std::optional<RunRecord> Database::last_run(const std::string& job_name) {
    static const char* kSql =
        "SELECT id, job_name, started_at, ended_at, status, exit_code,"
        "       duration_ms, stdout, stderr, trigger"
        " FROM runs WHERE job_name = ? ORDER BY started_at DESC, id DESC"
        " LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DbError(std::string("prepare last_run: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, job_name.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<RunRecord> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto text = [&](int col) -> std::string {
            const unsigned char* s = sqlite3_column_text(stmt, col);
            return s ? reinterpret_cast<const char*>(s) : "";
        };
        RunRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.job_name = text(1);
        r.started_at = sqlite3_column_int64(stmt, 2);
        r.ended_at = sqlite3_column_int64(stmt, 3);
        r.status = text(4);
        r.exit_code = sqlite3_column_int(stmt, 5);
        r.duration_ms = static_cast<long>(sqlite3_column_int64(stmt, 6));
        r.stdout_text = text(7);
        r.stderr_text = text(8);
        r.trigger = text(9);
        result = std::move(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::int64_t Database::insert_alert(const AlertRecord& alert) {
    static const char* kSql =
        "INSERT INTO alerts(ts, rule_name, severity, metric, kind, value,"
        "                   threshold, message, attribution)"
        " VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DbError(std::string("prepare insert_alert: ") +
                      sqlite3_errmsg(db_));
    }
    sqlite3_bind_int64(stmt, 1, alert.ts);
    sqlite3_bind_text(stmt, 2, alert.rule_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, alert.severity.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, alert.metric.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, alert.kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 6, alert.value);
    sqlite3_bind_double(stmt, 7, alert.threshold);
    sqlite3_bind_text(stmt, 8, alert.message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, alert.attribution.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw DbError(std::string("step insert_alert: ") + sqlite3_errmsg(db_));
    }
    return sqlite3_last_insert_rowid(db_);
}

std::vector<AlertRecord> Database::recent_alerts(int limit) {
    static const char* kSql =
        "SELECT id, ts, rule_name, severity, metric, kind, value, threshold,"
        "       message, attribution"
        " FROM alerts ORDER BY ts DESC, id DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DbError(std::string("prepare recent_alerts: ") +
                      sqlite3_errmsg(db_));
    }
    sqlite3_bind_int(stmt, 1, limit);

    std::vector<AlertRecord> out;
    auto text = [&](int col) -> std::string {
        const unsigned char* s = sqlite3_column_text(stmt, col);
        return s ? reinterpret_cast<const char*>(s) : "";
    };
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AlertRecord a;
        a.id = sqlite3_column_int64(stmt, 0);
        a.ts = sqlite3_column_int64(stmt, 1);
        a.rule_name = text(2);
        a.severity = text(3);
        a.metric = text(4);
        a.kind = text(5);
        a.value = sqlite3_column_double(stmt, 6);
        a.threshold = sqlite3_column_double(stmt, 7);
        a.message = text(8);
        a.attribution = text(9);
        out.push_back(std::move(a));
    }
    sqlite3_finalize(stmt);
    return out;
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
        "  ON metrics(metric, ts);"

        "CREATE TABLE IF NOT EXISTS runs("
        "  id          INTEGER PRIMARY KEY,"
        "  job_name    TEXT    NOT NULL,"
        "  started_at  INTEGER NOT NULL,"  // unix seconds
        "  ended_at    INTEGER NOT NULL,"
        "  status      TEXT    NOT NULL,"  // success/failed/timeout/spawn_error
        "  exit_code   INTEGER NOT NULL,"
        "  duration_ms INTEGER NOT NULL,"
        "  stdout      TEXT,"
        "  stderr      TEXT,"
        "  trigger     TEXT    NOT NULL);"  // schedule/manual/missed

        "CREATE INDEX IF NOT EXISTS idx_runs_job_started"
        "  ON runs(job_name, started_at);"

        "CREATE TABLE IF NOT EXISTS alerts("
        "  id         INTEGER PRIMARY KEY,"
        "  ts         INTEGER NOT NULL,"  // unix seconds
        "  rule_name  TEXT    NOT NULL,"
        "  severity   TEXT    NOT NULL,"
        "  metric     TEXT    NOT NULL,"
        "  kind       TEXT    NOT NULL,"  // firing/recovered
        "  value      REAL    NOT NULL,"
        "  threshold  REAL    NOT NULL,"
        "  message    TEXT,"
        "  attribution TEXT);"

        "CREATE INDEX IF NOT EXISTS idx_alerts_ts ON alerts(ts);");
}

}  // namespace agentpulse
