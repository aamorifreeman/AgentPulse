// AgentPulse test harness.
//
// A dependency-free set of checks for the M1 logic: CPU sampling, the SQLite
// metric store, the JSON request/response API, and the socket connection
// handler (exercised over a socketpair, since bind()/accept() need no test
// coverage and are blocked in some sandboxes).

#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "db.hpp"
#include "ipc/api.hpp"
#include "ipc/socket_server.hpp"
#include "metrics/cpu_sampler.hpp"
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

}  // namespace

int main() {
    test_cpu_sampler();
    test_database();
    test_api();
    test_socket_roundtrip();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
