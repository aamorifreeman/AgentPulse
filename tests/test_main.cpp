// AgentPulse test harness.
//
// A dependency-free set of checks for the M1 logic: CPU sampling, the SQLite
// metric store, the JSON request/response API, and the socket connection
// handler (exercised over a socketpair, since bind()/accept() need no test
// coverage and are blocked in some sandboxes).

#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "config/config.hpp"
#include "db.hpp"
#include "ipc/api.hpp"
#include "ipc/socket_server.hpp"
#include "jobs/process_runner.hpp"
#include "jobs/scheduler.hpp"
#include "metrics/cpu_sampler.hpp"
#include "metrics/disk_sampler.hpp"
#include "metrics/memory_sampler.hpp"
#include "metrics/process_sampler.hpp"
#include "metrics/thermal_sampler.hpp"
#include "schedule/cron.hpp"
#include "shared_state.hpp"

using json = nlohmann::json;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) {
        std::printf("  ok   %s\n", what.c_str());
    } else {
        ++g_failures;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

void test_cpu_sampler() {
    std::printf("[cpu_sampler]\n");
    agentpulse::CpuSampler cpu;
    check(cpu.sample() == 0.0, "first sample returns baseline 0");

    // Burn some CPU so the second interval has non-trivial busy ticks.
    volatile double sink = 0.0;
    for (long i = 0; i < 50'000'000; ++i) sink += i * 0.5;
    (void)sink;

    double pct = cpu.sample();
    check(pct >= 0.0 && pct <= 100.0, "second sample within [0,100]");
}

void test_database() {
    std::printf("[database]\n");
    std::string path = "/tmp/agentpulse_test_" +
                       std::to_string(::getpid()) + ".db";
    ::unlink(path.c_str());
    {
        agentpulse::Database db(path);
        agentpulse::migrate(db);

        check(!db.latest_metric("system.cpu.percent").has_value(),
              "no metric before insert");

        db.insert_metric(1000, "system.cpu.percent", 42.5);
        db.insert_metric(2000, "system.cpu.percent", 55.0);

        auto latest = db.latest_metric("system.cpu.percent");
        check(latest.has_value(), "latest_metric returns a value");
        check(latest && latest->first == 2000 && latest->second == 55.0,
              "latest_metric returns the newest sample");
    }
    ::unlink(path.c_str());
    ::unlink((path + "-wal").c_str());
    ::unlink((path + "-shm").c_str());
}

void test_api() {
    std::printf("[api]\n");
    agentpulse::SharedState state;
    state.set_cpu(33.3, 12345);
    agentpulse::Api api(state, /*started_at=*/999);

    auto ping = json::parse(api.handle("ping"));
    check(ping["ok"] == true && ping["reply"] == "pong", "ping -> pong");

    auto status = json::parse(api.handle("status"));
    check(status["ok"] == true, "status ok");
    check(status["cpu"]["valid"] == true, "status cpu valid");
    check(status["cpu"]["percent"] == 33.3, "status cpu percent");
    check(status["daemon"]["started_at"] == 999, "status started_at");

    auto json_form = json::parse(api.handle(R"({"cmd":"status"})"));
    check(json_form["ok"] == true, "JSON {\"cmd\":\"status\"} accepted");

    auto unknown = json::parse(api.handle("bogus"));
    check(unknown["ok"] == false, "unknown command rejected");
}

void test_socket_roundtrip() {
    std::printf("[socket serve_connection]\n");
    agentpulse::SharedState state;
    state.set_cpu(12.0, 777);
    agentpulse::Api api(state, 1);

    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        check(false, "socketpair created");
        return;
    }

    // Server side handles one connection on sv[1].
    std::thread server([&] {
        agentpulse::SocketServer::serve_connection(
            sv[1], [&api](const std::string& req) { return api.handle(req); });
        ::close(sv[1]);
    });

    // Client side writes a request and reads the response line on sv[0].
    std::string req = "status\n";
    ::write(sv[0], req.data(), req.size());

    std::string resp;
    char buf[512];
    while (true) {
        ssize_t r = ::read(sv[0], buf, sizeof(buf));
        if (r <= 0) break;
        resp.append(buf, static_cast<std::size_t>(r));
        if (resp.find('\n') != std::string::npos) break;
    }
    ::close(sv[0]);
    server.join();

    auto parsed = json::parse(resp, nullptr, false);
    check(!parsed.is_discarded(), "response is valid JSON");
    check(!parsed.is_discarded() && parsed["ok"] == true, "response ok");
    check(!parsed.is_discarded() && parsed["cpu"]["sampled_at"] == 777,
          "response carries live cpu snapshot");
}

void test_cron() {
    std::printf("[cron]\n");
    check(!agentpulse::CronSchedule::parse("nonsense").has_value(),
          "malformed expression rejected");
    check(!agentpulse::CronSchedule::parse("0 8 * *").has_value(),
          "wrong field count rejected");

    auto daily = agentpulse::CronSchedule::parse("0 8 * * *");
    check(daily.has_value(), "'0 8 * * *' parses");
    if (daily) {
        std::tm tm{};
        tm.tm_min = 0;
        tm.tm_hour = 8;
        tm.tm_mday = 15;
        tm.tm_mon = 5;
        tm.tm_year = 126;  // 2026
        std::time_t base = std::mktime(&tm);
        auto next = daily->next_after(base);
        check(next.has_value(), "next_after returns a time");
        if (next) {
            std::tm out{};
            localtime_r(&*next, &out);
            check(out.tm_hour == 8 && out.tm_min == 0,
                  "next daily run is at 08:00");
            check(*next > base, "next run is in the future");
        }
    }

    auto quarter = agentpulse::CronSchedule::parse("*/15 * * * *");
    check(quarter.has_value(), "'*/15 * * * *' parses");
    if (quarter) {
        std::time_t base = 1'700'000'000;
        auto next = quarter->next_after(base);
        if (next) {
            std::tm out{};
            localtime_r(&*next, &out);
            check(out.tm_min % 15 == 0, "step schedule lands on a quarter hour");
        }
    }
}

void test_config() {
    std::printf("[config]\n");
    std::string path = "/tmp/agentpulse_cfg_" + std::to_string(::getpid()) +
                       ".yaml";
    {
        std::ofstream f(path);
        f << "jobs:\n"
             "  - name: email-scan\n"
             "    command: echo scan\n"
             "    schedule: \"0 8 * * *\"\n"
             "    missed_run_policy: run_on_wake\n"
             "    timeout_seconds: 300\n"
             "    retries: 2\n";
    }
    auto cfg = agentpulse::load_config(path);
    check(cfg.jobs.size() == 1, "one job loaded");
    if (cfg.jobs.size() == 1) {
        const auto& j = cfg.jobs[0];
        check(j.name == "email-scan", "job name parsed");
        check(j.command == "echo scan", "job command parsed");
        check(j.schedule.has_value(), "job schedule parsed");
        check(j.missed_run_policy == agentpulse::MissedRunPolicy::RunOnWake,
              "missed_run_policy parsed");
        check(j.timeout_seconds == 300, "timeout parsed");
        check(j.retries == 2, "retries parsed");
    }
    ::unlink(path.c_str());

    // A job missing 'command' must be rejected.
    std::string bad = path + ".bad";
    {
        std::ofstream f(bad);
        f << "jobs:\n  - name: broken\n";
    }
    bool threw = false;
    try {
        agentpulse::load_config(bad);
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "job missing command is rejected");
    ::unlink(bad.c_str());

    check(agentpulse::load_config("/tmp/does-not-exist-xyz.yaml").jobs.empty(),
          "missing file yields empty config");
}

void test_process_runner() {
    std::printf("[process_runner]\n");
    auto ok = agentpulse::run_command("echo hello", 0);
    check(ok.status == agentpulse::RunStatus::Success, "echo succeeds");
    check(ok.exit_code == 0, "echo exit 0");
    check(ok.stdout_text.find("hello") != std::string::npos,
          "stdout captured");

    auto fail = agentpulse::run_command("exit 3", 0);
    check(fail.status == agentpulse::RunStatus::Failed, "exit 3 -> failed");
    check(fail.exit_code == 3, "exit code captured");

    auto err = agentpulse::run_command("echo oops 1>&2", 0);
    check(err.stderr_text.find("oops") != std::string::npos,
          "stderr captured");

    auto slow = agentpulse::run_command("sleep 5", 1);
    check(slow.status == agentpulse::RunStatus::Timeout,
          "long command times out");
}

void test_scheduler_manual_run() {
    std::printf("[scheduler manual run]\n");
    std::string db_path = "/tmp/agentpulse_sched_" +
                          std::to_string(::getpid()) + ".db";
    ::unlink(db_path.c_str());

    agentpulse::Job job;
    job.name = "hello";
    job.command = "echo scheduler-ran";

    agentpulse::SharedState state;
    {
        agentpulse::Scheduler sched({job}, db_path, state);
        sched.start();

        check(sched.request_run("hello"), "manual run accepted");
        check(!sched.request_run("nope"), "unknown job rejected");

        // Wait for the run to complete (bounded).
        bool done = false;
        for (int i = 0; i < 40 && !done; ++i) {
            for (const auto& s : state.jobs()) {
                if (s.name == "hello" && s.has_last_run && !s.running) {
                    done = true;
                    check(s.last_status == "success", "manual run succeeded");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        check(done, "manual run completed and published");
        sched.stop();
    }

    // Verify the run was persisted.
    agentpulse::Database db(db_path);
    auto last = db.last_run("hello");
    check(last.has_value(), "run persisted to database");
    check(last && last->trigger == "manual", "run trigger recorded");

    ::unlink(db_path.c_str());
    ::unlink((db_path + "-wal").c_str());
    ::unlink((db_path + "-shm").c_str());
}

void test_memory_sampler() {
    std::printf("[memory_sampler]\n");
    auto m = agentpulse::sample_memory();
    check(m.valid, "memory sample valid");
    check(m.total_bytes > 0, "total memory > 0");
    check(m.used_bytes <= m.total_bytes, "used <= total");
    check(m.used_percent >= 0.0 && m.used_percent <= 100.0,
          "memory used_percent within [0,100]");
}

void test_disk_sampler() {
    std::printf("[disk_sampler]\n");
    auto d = agentpulse::sample_disk("/");
    check(d.valid, "disk sample valid");
    check(d.total_bytes > 0, "total disk > 0");
    check(d.available_bytes > 0, "available disk > 0");
    check(d.used_percent >= 0.0 && d.used_percent <= 100.0,
          "disk used_percent within [0,100]");
}

void test_thermal_sampler() {
    std::printf("[thermal_sampler]\n");
    auto t = agentpulse::sample_thermal_state();
    std::string s = agentpulse::to_string(t);
    check(s == "nominal" || s == "fair" || s == "serious" || s == "critical",
          "thermal state is a known label");
    check(agentpulse::thermal_state_from_string("serious") ==
              agentpulse::ThermalState::Serious,
          "thermal string round-trips");
    check(agentpulse::thermal_state_from_string("bogus") ==
              agentpulse::ThermalState::Nominal,
          "unknown thermal string defaults to nominal");
}

void test_process_sampler() {
    std::printf("[process_sampler]\n");
    agentpulse::ProcessSampler ps;
    auto first = ps.top(5);
    check(!first.empty(), "process list non-empty");
    check(first.size() <= 5, "top(5) returns at most 5");
    bool any_rss = false;
    for (const auto& p : first) {
        if (p.rss_bytes > 0) any_rss = true;
        if (p.cpu_percent < 0.0) {
            check(false, "cpu_percent is non-negative");
        }
    }
    check(any_rss, "at least one process reports RSS");

    // Burn some CPU, then a second sample should compute non-negative CPU%.
    volatile double sink = 0.0;
    for (long i = 0; i < 30'000'000; ++i) sink += i * 0.3;
    (void)sink;
    auto second = ps.top(5);
    check(!second.empty(), "second process sample non-empty");
    check(second.size() >= 2 ? second[0].cpu_percent >= second[1].cpu_percent
                             : true,
          "processes sorted by cpu descending");
}

}  // namespace

int main() {
    test_cpu_sampler();
    test_database();
    test_api();
    test_socket_roundtrip();
    test_cron();
    test_config();
    test_process_runner();
    test_scheduler_manual_run();
    test_memory_sampler();
    test_disk_sampler();
    test_thermal_sampler();
    test_process_sampler();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
