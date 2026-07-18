// AgentPulse daemon entry point.
//
// M1 scope: sample overall CPU utilization on a timer, persist samples to
// SQLite, and serve the live value over a Unix-domain socket (newline JSON).
// The sampler runs on the main thread; the socket server runs on its own
// thread. Later milestones add jobs, more collectors, and the alert engine.

#include <csignal>
#include <chrono>
#include <ctime>
#include <memory>
#include <string>
#include <thread>

#include <sqlite3.h>

#include "db.hpp"
#include "ipc/api.hpp"
#include "ipc/socket_server.hpp"
#include "log.hpp"
#include "metrics/cpu_sampler.hpp"
#include "paths.hpp"
#include "shared_state.hpp"

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
    signal(SIGPIPE, SIG_IGN);
}

// Samples CPU every `interval`, persisting to the database and publishing the
// latest value to shared state. Returns when g_stop becomes non-zero.
void sampler_loop(agentpulse::Database& db, agentpulse::SharedState& state) {
    using namespace std::chrono_literals;
    constexpr auto interval = 5s;
    constexpr auto poll_slice = 200ms;

    agentpulse::CpuSampler cpu;
    cpu.sample();  // establish baseline; first real reading next tick

    auto next = std::chrono::steady_clock::now();
    while (g_stop == 0) {
        auto now = std::chrono::steady_clock::now();
        if (now >= next) {
            const double percent = cpu.sample();
            const std::int64_t ts = std::time(nullptr);
            state.set_cpu(percent, ts);
            try {
                db.insert_metric(ts, "system.cpu.percent", percent);
            } catch (const agentpulse::DbError& e) {
                agentpulse::log_warn(std::string("metric insert failed: ") +
                                     e.what());
            }
            next = now + interval;
        }
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

    // Open the database and apply the schema.
    std::unique_ptr<agentpulse::Database> db;
    try {
        db = std::make_unique<agentpulse::Database>(paths.database().string());
        agentpulse::migrate(*db);
    } catch (const agentpulse::DbError& e) {
        agentpulse::log_error(std::string("database init failed: ") + e.what());
        return 1;
    }

    agentpulse::SharedState state;
    const std::int64_t started_at = std::time(nullptr);
    agentpulse::Api api(state, started_at);

    agentpulse::SocketServer server;
    try {
        server.start(paths.socket().string(),
                     [&api](const std::string& req) { return api.handle(req); });
    } catch (const std::exception& e) {
        agentpulse::log_error(std::string("socket start failed: ") + e.what());
        return 1;
    }

    install_signal_handlers();
    agentpulse::log_info("ready");

    sampler_loop(*db, state);

    agentpulse::log_info(std::string("received signal ") +
                         std::to_string(static_cast<int>(g_stop)) +
                         ", shutting down");
    server.stop();
    return 0;
}
