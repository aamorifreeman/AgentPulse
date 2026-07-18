// AgentPulse daemon entry point.
//
// M0 scope: start cleanly, prove the toolchain (C++20 + SQLite linkage),
// establish the per-user data directories, run a heartbeat loop, and shut
// down gracefully on SIGTERM/SIGINT. Metric collection, scheduling, and the
// socket API arrive in later milestones.

#include <csignal>
#include <cstring>
#include <string>
#include <thread>

#include <sqlite3.h>

#include "log.hpp"
#include "paths.hpp"

namespace {

// Set from a signal handler, so it must be async-signal-safe.
volatile std::sig_atomic_t g_stop = 0;

extern "C" void handle_signal(int signum) {
    g_stop = signum;
}

void install_signal_handlers() {
    struct sigaction sa {};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    // Don't die if a child pipe closes; we'll handle child I/O explicitly later.
    signal(SIGPIPE, SIG_IGN);
}

// Opens (creating if needed) the SQLite database and ensures a meta table
// exists. Later milestones add the real schema via a migration module.
bool init_database(const std::string& db_path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        agentpulse::log_error(std::string("cannot open database: ") +
                              sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    char* err = nullptr;
    const char* schema =
        "CREATE TABLE IF NOT EXISTS meta("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL);";
    if (sqlite3_exec(db, schema, nullptr, nullptr, &err) != SQLITE_OK) {
        agentpulse::log_error(std::string("schema init failed: ") +
                              (err ? err : "unknown"));
        sqlite3_free(err);
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return true;
}

void run_loop() {
    using namespace std::chrono_literals;
    constexpr auto heartbeat_interval = 30s;
    constexpr auto poll_slice = 200ms;

    auto next_beat = std::chrono::steady_clock::now();
    while (g_stop == 0) {
        auto now = std::chrono::steady_clock::now();
        if (now >= next_beat) {
            agentpulse::log_debug("heartbeat");
            next_beat = now + heartbeat_interval;
        }
        // Sleep in small slices so signals are noticed promptly.
        std::this_thread::sleep_for(poll_slice);
    }
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--version" || arg == "-v") {
            std::printf("agentpulsed %s\n", AGENTPULSE_VERSION);
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            std::printf(
                "agentpulsed %s — AgentPulse daemon\n"
                "Usage: agentpulsed [--version] [--help]\n",
                AGENTPULSE_VERSION);
            return 0;
        }
        agentpulse::log_warn("ignoring unknown argument: " + arg);
    }

    agentpulse::log_info(std::string("agentpulsed ") + AGENTPULSE_VERSION +
                         " starting (sqlite " + sqlite3_libversion() + ")");

    agentpulse::Paths paths;
    try {
        paths = agentpulse::resolve_paths();
    } catch (const std::exception& e) {
        agentpulse::log_error(std::string("path setup failed: ") + e.what());
        return 1;
    }
    agentpulse::log_info("data dir: " + paths.data_dir.string());

    if (!init_database(paths.database().string())) {
        return 1;
    }

    install_signal_handlers();
    agentpulse::log_info("ready");

    run_loop();

    agentpulse::log_info(std::string("received signal ") +
                         std::to_string(static_cast<int>(g_stop)) +
                         ", shutting down");
    return 0;
}
