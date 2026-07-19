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

#include "alerts/alert_engine.hpp"
#include "alerts/notifier.hpp"
#include "config/config.hpp"
#include "db.hpp"
#include "ipc/api.hpp"
#include "ipc/socket_server.hpp"
#include "jobs/scheduler.hpp"
#include "log.hpp"
#include "metrics/cpu_sampler.hpp"
#include "metrics/disk_sampler.hpp"
#include "metrics/memory_sampler.hpp"
#include "metrics/process_sampler.hpp"
#include "metrics/self_sampler.hpp"
#include "metrics/thermal_sampler.hpp"
#include "paths.hpp"
#include "power/sleep_monitor.hpp"
#include "shared_state.hpp"

namespace {

// Set from signal handlers, so they must be async-signal-safe.
volatile std::sig_atomic_t g_stop = 0;
volatile std::sig_atomic_t g_reload = 0;

extern "C" void handle_signal(int signum) {
    if (signum == SIGHUP) {
        g_reload = 1;
    } else {
        g_stop = signum;
    }
}

void install_signal_handlers() {
    struct sigaction sa {};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);  // reload config
    signal(SIGPIPE, SIG_IGN);
}

// Samples CPU and system health every `interval`, evaluates alert rules,
// persisting metrics/alerts to the database and publishing to shared state.
// Returns when g_stop becomes non-zero.
void sampler_loop(agentpulse::Database& db, agentpulse::SharedState& state,
                  const std::string& config_path,
                  std::vector<agentpulse::Rule> rules,
                  agentpulse::QuietHours quiet) {
    using namespace std::chrono_literals;
    constexpr auto interval = 5s;
    constexpr auto poll_slice = 200ms;
    constexpr std::size_t kRecentAlerts = 25;

    agentpulse::CpuSampler cpu;
    agentpulse::ProcessSampler procs;
    agentpulse::SelfSampler self;
    auto engine = std::make_unique<agentpulse::AlertEngine>(std::move(rules),
                                                            quiet);
    std::vector<agentpulse::AlertInfo> recent_alerts;
    cpu.sample();     // establish CPU baseline
    procs.top(5);     // establish per-process CPU baseline
    self.sample();    // establish self CPU baseline

    auto next = std::chrono::steady_clock::now();
    while (g_stop == 0) {
        // Config hot-reload (SIGHUP): rebuild the alert engine from disk.
        // Job changes require a restart; rules and quiet hours reload live.
        if (g_reload) {
            g_reload = 0;
            try {
                auto cfg = agentpulse::load_config(config_path);
                engine = std::make_unique<agentpulse::AlertEngine>(
                    cfg.rules, cfg.quiet_hours);
                agentpulse::log_info(
                    "config reloaded: " + std::to_string(cfg.rules.size()) +
                    " rule(s) (job changes need a restart)");
            } catch (const std::exception& e) {
                agentpulse::log_warn(std::string("config reload failed: ") +
                                     e.what());
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= next) {
            const std::int64_t ts = std::time(nullptr);
            const double cpu_pct = cpu.sample();
            const agentpulse::MemorySample mem = agentpulse::sample_memory();
            const agentpulse::DiskSample disk = agentpulse::sample_disk();
            const agentpulse::ThermalState thermal =
                agentpulse::sample_thermal_state();
            auto top = procs.top(5);
            const agentpulse::SelfStats self_stats = self.sample();

            state.set_cpu(cpu_pct, ts);
            if (self_stats.valid) {
                state.set_self(self_stats.rss_bytes, self_stats.cpu_percent);
            }

            agentpulse::SystemSnapshot sys;
            sys.valid = true;
            sys.sampled_at = ts;
            if (mem.valid) {
                sys.mem_total_bytes = mem.total_bytes;
                sys.mem_used_bytes = mem.used_bytes;
                sys.mem_used_percent = mem.used_percent;
            }
            if (disk.valid) {
                sys.disk_total_bytes = disk.total_bytes;
                sys.disk_available_bytes = disk.available_bytes;
                sys.disk_used_percent = disk.used_percent;
            }
            sys.thermal_state = agentpulse::to_string(thermal);
            sys.top_processes = top;
            state.set_system(sys);

            try {
                db.insert_metric(ts, "system.cpu.percent", cpu_pct);
                if (mem.valid)
                    db.insert_metric(ts, "system.memory.percent",
                                     mem.used_percent);
                if (disk.valid) {
                    db.insert_metric(ts, "system.disk.percent",
                                     disk.used_percent);
                    db.insert_metric(
                        ts, "system.disk.available_bytes",
                        static_cast<double>(disk.available_bytes));
                }
                db.insert_metric(ts, "system.thermal.level",
                                 static_cast<double>(thermal));
                if (self_stats.valid) {
                    db.insert_metric(ts, "daemon.rss_bytes",
                                     static_cast<double>(self_stats.rss_bytes));
                    db.insert_metric(ts, "daemon.cpu.percent",
                                     self_stats.cpu_percent);
                }
            } catch (const agentpulse::DbError& e) {
                agentpulse::log_warn(std::string("metric insert failed: ") +
                                     e.what());
            }

            // Evaluate alert rules against the freshest values.
            agentpulse::AlertEngine::MetricMap metric_map;
            metric_map["system.cpu.percent"] = cpu_pct;
            if (mem.valid)
                metric_map["system.memory.percent"] = mem.used_percent;
            if (disk.valid) {
                metric_map["system.disk.percent"] = disk.used_percent;
                metric_map["system.disk.available_gb"] =
                    static_cast<double>(disk.available_bytes) / 1e9;
            }
            metric_map["system.thermal_state"] =
                static_cast<double>(thermal);

            std::string attribution;
            if (!top.empty()) {
                char b[128];
                std::snprintf(b, sizeof(b), "%s — %.0f%% CPU",
                              top.front().name.c_str(),
                              top.front().cpu_percent);
                attribution = b;
            }

            auto events = engine->evaluate(metric_map, ts, attribution);
            for (const auto& e : events) {
                agentpulse::AlertRecord rec;
                rec.ts = e.ts;
                rec.rule_name = e.rule_name;
                rec.severity = e.severity;
                rec.metric = e.metric;
                rec.kind = e.kind;
                rec.value = e.value;
                rec.threshold = e.threshold;
                rec.message = e.message;
                rec.attribution = e.attribution;
                try {
                    db.insert_alert(rec);
                } catch (const agentpulse::DbError& ex) {
                    agentpulse::log_warn(std::string("insert_alert failed: ") +
                                         ex.what());
                }
                agentpulse::log_info("ALERT " + e.kind + " [" + e.severity +
                                     "] " + e.message);
                if (e.notify) {
                    std::string body = e.message;
                    if (!e.attribution.empty()) body += "\n" + e.attribution;
                    agentpulse::send_notification("AgentPulse: " + e.rule_name,
                                                  body);
                }

                agentpulse::AlertInfo info;
                info.ts = e.ts;
                info.rule_name = e.rule_name;
                info.severity = e.severity;
                info.metric = e.metric;
                info.kind = e.kind;
                info.value = e.value;
                info.threshold = e.threshold;
                info.message = e.message;
                info.attribution = e.attribution;
                info.notify = e.notify;
                recent_alerts.insert(recent_alerts.begin(), std::move(info));
            }
            if (recent_alerts.size() > kRecentAlerts) {
                recent_alerts.resize(kRecentAlerts);
            }
            if (!events.empty()) {
                state.set_alerts(recent_alerts);
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
    agentpulse::Api api(state, started_at, paths.database().string());

    // Load jobs + rules and start the scheduler (system monitoring still runs
    // if the config is absent or empty).
    std::unique_ptr<agentpulse::Scheduler> scheduler;
    std::vector<agentpulse::Rule> rules;
    agentpulse::QuietHours quiet;
    try {
        agentpulse::Config config =
            agentpulse::load_config(paths.config_file().string());
        agentpulse::log_info("loaded " + std::to_string(config.jobs.size()) +
                             " job(s), " + std::to_string(config.rules.size()) +
                             " rule(s) from config");
        rules = config.rules;
        quiet = config.quiet_hours;
        scheduler = std::make_unique<agentpulse::Scheduler>(
            std::move(config.jobs), paths.database().string(), state);
        api.set_run_handler([&scheduler](const std::string& name) {
            return scheduler->request_run(name);
        });
        api.set_add_job_handler([&scheduler](const agentpulse::Job& job) {
            return scheduler->add_job(job);
        });
        api.set_remove_job_handler([&scheduler](const std::string& name) {
            return scheduler->remove_job(name);
        });
        scheduler->start();
    } catch (const std::exception& e) {
        agentpulse::log_error(std::string("scheduler init failed: ") +
                              e.what());
        return 1;
    }

    agentpulse::SocketServer server;
    try {
        server.start(paths.socket().string(),
                     [&api](const std::string& req) { return api.handle(req); });
    } catch (const std::exception& e) {
        // Non-fatal: the daemon still samples metrics and runs jobs; only the
        // query API is unavailable.
        agentpulse::log_warn(std::string("socket unavailable: ") + e.what() +
                             " — continuing without the query API");
    }

    install_signal_handlers();
    agentpulse::log_info("ready");

    // Watch for sleep/wake so missed runs are re-evaluated the moment the
    // machine wakes (the sampler loop's gap detection is the fallback).
    agentpulse::SleepMonitor sleep_monitor;
    sleep_monitor.start([&scheduler] { scheduler->notify_wake(); });

    sampler_loop(*db, state, paths.config_file().string(), std::move(rules),
                 quiet);

    agentpulse::log_info(std::string("received signal ") +
                         std::to_string(static_cast<int>(g_stop)) +
                         ", shutting down");
    sleep_monitor.stop();
    server.stop();
    scheduler->stop();
    return 0;
}
