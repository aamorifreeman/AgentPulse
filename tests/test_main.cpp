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

#include "alerts/alert_engine.hpp"
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
#include "metrics/self_sampler.hpp"
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
        auto prev = quarter->prev_before(base);
        if (prev) {
            check(*prev <= base, "prev_before is at or before the time");
            std::tm out{};
            localtime_r(&*prev, &out);
            check(out.tm_min % 15 == 0, "prev_before lands on a quarter hour");
        }
    }
}

void test_missed_run_detection() {
    std::printf("[missed_run detection]\n");
    auto daily = agentpulse::CronSchedule::parse("0 8 * * *");
    if (!daily) {
        check(false, "daily schedule parses");
        return;
    }
    // 2026-06-15 12:00 local — the 08:00 occurrence already passed today.
    std::tm tm{};
    tm.tm_min = 0; tm.tm_hour = 12; tm.tm_mday = 15; tm.tm_mon = 5;
    tm.tm_year = 126;
    std::time_t noon = std::mktime(&tm);

    // Never ran: the 08:00 occurrence counts as missed.
    auto missed = agentpulse::detect_missed_run(*daily, 0, noon);
    check(missed.has_value(), "missed run detected when never run");

    // Ran at 08:05 today: not missed.
    std::tm tm2 = tm; tm2.tm_hour = 8; tm2.tm_min = 5;
    std::time_t ran_today = std::mktime(&tm2);
    auto not_missed = agentpulse::detect_missed_run(*daily, ran_today, noon);
    check(!not_missed.has_value(), "not missed when it already ran today");

    // Ran yesterday only: today's 08:00 is missed.
    std::time_t yesterday = ran_today - 24 * 3600;
    auto missed2 = agentpulse::detect_missed_run(*daily, yesterday, noon);
    check(missed2.has_value(), "missed when last run was yesterday");
}

void test_scheduler_retries_and_missed() {
    std::printf("[scheduler retries + missed]\n");
    std::string db_path = "/tmp/agentpulse_retry_" +
                          std::to_string(::getpid()) + ".db";
    ::unlink(db_path.c_str());

    // A job that always fails, with 2 retries -> 3 total attempts.
    agentpulse::Job job;
    job.name = "flaky";
    job.command = "exit 1";
    job.retries = 2;

    agentpulse::SharedState state;
    {
        agentpulse::Scheduler sched({job}, db_path, state);
        sched.start();
        sched.request_run("flaky");

        bool done = false;
        for (int i = 0; i < 100 && !done; ++i) {
            for (const auto& s : state.jobs()) {
                if (s.name == "flaky" && s.has_last_run && !s.running) {
                    done = true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        check(done, "flaky job completed");
        sched.stop();
    }

    // All three attempts should have been persisted as failed runs.
    agentpulse::Database db(db_path);
    // Count via last_run + a manual query would be nicer; use recent by
    // re-opening: we only have last_run, so verify at least the final failed.
    auto last = db.last_run("flaky");
    check(last.has_value() && last->status == "failed",
          "final attempt recorded as failed");
    check(db.count_runs("flaky") == 3, "all 3 attempts (1 + 2 retries) persisted");

    ::unlink(db_path.c_str());
    ::unlink((db_path + "-wal").c_str());
    ::unlink((db_path + "-shm").c_str());
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

void test_dynamic_jobs() {
    std::printf("[dynamic jobs]\n");
    std::string db_path = "/tmp/agentpulse_dyn_" +
                          std::to_string(::getpid()) + ".db";
    ::unlink(db_path.c_str());

    agentpulse::SharedState state;
    {
        agentpulse::Scheduler sched({}, db_path, state);  // no config jobs
        sched.start();

        agentpulse::Job job;
        job.name = "ui-job";
        job.command = "echo hi";
        check(sched.add_job(job).empty(), "add_job succeeds");
        check(!sched.add_job(job).empty(), "duplicate add rejected");
        check(!sched.remove_job("missing").empty(), "remove unknown rejected");
        check(sched.remove_job("ui-job").empty(), "remove ui job succeeds");
        sched.stop();
    }

    // Add a job, then confirm it persists across a fresh scheduler instance.
    {
        agentpulse::Scheduler sched({}, db_path, state);
        agentpulse::Job job;
        job.name = "persisted";
        job.command = "echo hi";
        check(sched.add_job(job).empty(), "add persisted job");
    }
    {
        agentpulse::Scheduler sched({}, db_path, state);
        // Reloaded from job_defs with source=ui, so removal should succeed.
        check(sched.remove_job("persisted").empty(),
              "persisted job reloaded and removable");
    }

    ::unlink(db_path.c_str());
    ::unlink((db_path + "-wal").c_str());
    ::unlink((db_path + "-shm").c_str());
}

void test_self_sampler() {
    std::printf("[self_sampler]\n");
    agentpulse::SelfSampler s;
    auto a = s.sample();
    check(a.valid, "self sample valid");
    check(a.rss_bytes > 0, "self RSS > 0");
    auto b = s.sample();
    check(b.cpu_percent >= 0.0, "self cpu_percent non-negative");
}

void test_history_and_runs() {
    std::printf("[history + runs api]\n");
    std::string db_path = "/tmp/agentpulse_hist_" +
                          std::to_string(::getpid()) + ".db";
    ::unlink(db_path.c_str());
    const std::int64_t now = std::time(nullptr);
    {
        agentpulse::Database db(db_path);
        agentpulse::migrate(db);
        db.insert_metric(now - 10, "testm", 5.0);
        db.insert_metric(now - 5, "testm", 7.0);
        for (int i = 0; i < 2; ++i) {
            agentpulse::RunRecord r;
            r.job_name = "jx";
            r.started_at = now - 10 + i;
            r.ended_at = now - 9 + i;
            r.status = "success";
            r.exit_code = 0;
            r.duration_ms = 100;
            r.trigger = "manual";
            db.insert_run(r);
        }
    }

    agentpulse::SharedState state;
    agentpulse::Api api(state, 1, db_path);

    auto h = json::parse(
        api.handle(R"({"cmd":"history","metric":"testm","seconds":3600})"));
    check(h["ok"] == true, "history ok");
    check(h["points"].size() == 2, "history returns both points");

    auto r = json::parse(api.handle("runs jx 10"));
    check(r["ok"] == true, "runs ok");
    check(r["runs"].size() == 2, "runs returns both records");

    // Unavailable when no db is attached.
    agentpulse::Api api_nodb(state, 1);
    auto e = json::parse(api_nodb.handle("history testm"));
    check(e["ok"] == false, "history unavailable without db");

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

agentpulse::Rule make_rule(const std::string& metric,
                           agentpulse::Condition cond, double threshold,
                           int duration, int cooldown,
                           const std::string& severity = "warning") {
    agentpulse::Rule r;
    r.name = metric + "-rule";
    r.metric = metric;
    r.condition = cond;
    r.threshold = threshold;
    r.duration_seconds = duration;
    r.cooldown_seconds = cooldown;
    r.severity = severity;
    return r;
}

void test_alert_engine() {
    std::printf("[alert_engine]\n");
    using agentpulse::Condition;

    // Duration gate: must exceed threshold for 60s before firing.
    {
        agentpulse::AlertEngine eng(
            {make_rule("system.cpu.percent", Condition::GreaterThan, 90, 60,
                       300)},
            {});
        auto e0 = eng.evaluate({{"system.cpu.percent", 95}}, 1000);
        check(e0.empty(), "no fire before duration elapses");
        auto e1 = eng.evaluate({{"system.cpu.percent", 95}}, 1030);
        check(e1.empty(), "still no fire at 30s");
        auto e2 = eng.evaluate({{"system.cpu.percent", 95}}, 1060);
        check(e2.size() == 1 && e2[0].kind == "firing",
              "fires once duration elapsed");

        // Recovery when value drops back below threshold.
        auto e3 = eng.evaluate({{"system.cpu.percent", 10}}, 1090);
        check(e3.size() == 1 && e3[0].kind == "recovered",
              "recovers when condition clears");
    }

    // Cooldown: after recovery, re-fire is suppressed until cooldown passes.
    {
        agentpulse::AlertEngine eng(
            {make_rule("system.cpu.percent", Condition::GreaterThan, 90, 0,
                       300)},
            {});
        auto a = eng.evaluate({{"system.cpu.percent", 95}}, 1000);
        check(a.size() == 1 && a[0].kind == "firing", "fires immediately (0s duration)");
        eng.evaluate({{"system.cpu.percent", 10}}, 1010);  // recover
        auto b = eng.evaluate({{"system.cpu.percent", 95}}, 1100);
        check(b.empty(), "re-fire suppressed within cooldown");
        auto c = eng.evaluate({{"system.cpu.percent", 95}}, 1400);
        check(c.size() == 1 && c[0].kind == "firing",
              "re-fires after cooldown elapses");
    }

    // Less-than for low disk, and attribution on CPU rules.
    {
        agentpulse::AlertEngine eng(
            {make_rule("system.disk.available_gb", Condition::LessThan, 10, 0,
                       0)},
            {});
        auto e = eng.evaluate({{"system.disk.available_gb", 8.4}}, 500);
        check(e.size() == 1 && e[0].kind == "firing", "low disk fires");
    }
    {
        agentpulse::AlertEngine eng(
            {make_rule("system.cpu.percent", Condition::GreaterThan, 90, 0, 0)},
            {});
        auto e = eng.evaluate({{"system.cpu.percent", 99}}, 100,
                              "Chrome — 184% CPU");
        check(e.size() == 1 && e[0].attribution == "Chrome — 184% CPU",
              "cpu alert carries process attribution");
    }

    // Quiet hours suppress notification for non-critical, but not critical.
    {
        agentpulse::QuietHours qh;
        qh.enabled = true;
        qh.start_hour = 0;
        qh.end_hour = 24;  // always quiet for the test
        agentpulse::AlertEngine eng(
            {make_rule("system.cpu.percent", Condition::GreaterThan, 90, 0, 0,
                       "warning"),
             make_rule("system.memory.percent", Condition::GreaterThan, 90, 0,
                       0, "critical")},
            qh);
        auto e = eng.evaluate(
            {{"system.cpu.percent", 95}, {"system.memory.percent", 95}}, 100);
        bool warn_suppressed = false, crit_notifies = false;
        for (const auto& ev : e) {
            if (ev.severity == "warning" && !ev.notify) warn_suppressed = true;
            if (ev.severity == "critical" && ev.notify) crit_notifies = true;
        }
        check(warn_suppressed, "quiet hours suppress non-critical notify");
        check(crit_notifies, "critical alerts notify even in quiet hours");
    }
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
    test_alert_engine();
    test_missed_run_detection();
    test_scheduler_retries_and_missed();
    test_dynamic_jobs();
    test_self_sampler();
    test_history_and_runs();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
