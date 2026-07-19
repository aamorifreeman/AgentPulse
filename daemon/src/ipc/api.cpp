#include "ipc/api.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <sstream>

#include <nlohmann/json.hpp>

#include "config/config.hpp"
#include "db.hpp"
#include "shared_state.hpp"

namespace agentpulse {

using json = nlohmann::json;

namespace {

struct Request {
    std::string cmd;
    std::string arg;   // job name / metric name
    std::string arg2;  // seconds / limit
};

// Parses a request line: a JSON object {"cmd":..., "job"/"metric":...,
// "seconds"/"limit":...} or a bare "cmd [arg] [arg2]" token sequence.
Request parse_request(const std::string& line) {
    auto parsed = json::parse(line, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_object() && parsed.contains("cmd") &&
        parsed["cmd"].is_string()) {
        Request r;
        r.cmd = parsed["cmd"].get<std::string>();
        for (const char* key : {"job", "metric"}) {
            if (parsed.contains(key) && parsed[key].is_string()) {
                r.arg = parsed[key].get<std::string>();
            }
        }
        for (const char* key : {"seconds", "limit"}) {
            if (parsed.contains(key) && parsed[key].is_number()) {
                r.arg2 = std::to_string(parsed[key].get<long long>());
            }
        }
        return r;
    }

    Request r;
    std::istringstream ss(line);
    ss >> r.cmd >> r.arg >> r.arg2;
    return r;
}

json job_to_json(const JobStatus& s) {
    json j{
        {"name", s.name},
        {"schedule", s.schedule_expr},
        {"source", s.source},
        {"next_run", s.next_run},
        {"running", s.running},
    };
    if (s.has_last_run) {
        j["last_run"] = {
            {"status", s.last_status},
            {"started_at", s.last_started_at},
            {"exit_code", s.last_exit_code},
            {"duration_ms", s.last_duration_ms},
            {"trigger", s.last_trigger},
        };
    } else {
        j["last_run"] = nullptr;
    }
    return j;
}

json jobs_array(const SharedState& state) {
    json arr = json::array();
    for (const auto& s : state.jobs()) {
        arr.push_back(job_to_json(s));
    }
    return arr;
}

json system_to_json(const SystemSnapshot& s) {
    json procs = json::array();
    for (const auto& p : s.top_processes) {
        procs.push_back({
            {"pid", p.pid},
            {"name", p.name},
            {"cpu_percent", p.cpu_percent},
            {"rss_bytes", p.rss_bytes},
        });
    }
    return json{
        {"valid", s.valid},
        {"sampled_at", s.sampled_at},
        {"memory",
         {{"total_bytes", s.mem_total_bytes},
          {"used_bytes", s.mem_used_bytes},
          {"used_percent", s.mem_used_percent}}},
        {"disk",
         {{"total_bytes", s.disk_total_bytes},
          {"available_bytes", s.disk_available_bytes},
          {"used_percent", s.disk_used_percent}}},
        {"thermal_state", s.thermal_state},
        {"top_processes", procs},
    };
}

json alerts_array(const SharedState& state) {
    json arr = json::array();
    for (const auto& a : state.alerts()) {
        arr.push_back({
            {"ts", a.ts},
            {"rule", a.rule_name},
            {"severity", a.severity},
            {"metric", a.metric},
            {"kind", a.kind},
            {"value", a.value},
            {"threshold", a.threshold},
            {"message", a.message},
            {"attribution", a.attribution},
            {"notified", a.notify},
        });
    }
    return arr;
}

}  // namespace

Api::Api(const SharedState& state, std::int64_t started_at,
         const std::string& db_path)
    : state_(state), started_at_(started_at) {
    if (!db_path.empty()) {
        try {
            db_ = std::make_unique<Database>(db_path);
        } catch (const DbError&) {
            db_.reset();  // history/runs will report unavailable
        }
    }
}

Api::~Api() = default;

std::string Api::handle(const std::string& request) {
    const Request req = parse_request(request);

    if (req.cmd == "ping") {
        return json{{"ok", true}, {"cmd", "ping"}, {"reply", "pong"}}.dump();
    }

    if (req.cmd == "status") {
        const SharedState::Cpu cpu = state_.cpu();
        const SharedState::Self self = state_.self();
        json j{
            {"ok", true},
            {"cmd", "status"},
            {"daemon",
             {{"started_at", started_at_},
              {"rss_bytes", self.rss_bytes},
              {"cpu_percent", self.cpu_percent}}},
            {"cpu",
             {{"valid", cpu.valid},
              {"percent", cpu.percent},
              {"sampled_at", cpu.sampled_at}}},
            {"system", system_to_json(state_.system())},
            {"jobs", jobs_array(state_)},
            {"alerts", alerts_array(state_)},
        };
        return j.dump();
    }

    if (req.cmd == "jobs") {
        return json{{"ok", true}, {"cmd", "jobs"}, {"jobs", jobs_array(state_)}}
            .dump();
    }

    if (req.cmd == "alerts") {
        return json{{"ok", true},
                    {"cmd", "alerts"},
                    {"alerts", alerts_array(state_)}}
            .dump();
    }

    if (req.cmd == "history") {
        if (!db_) {
            return json{{"ok", false}, {"error", "history unavailable"}}.dump();
        }
        if (req.arg.empty()) {
            return json{{"ok", false}, {"error", "missing metric"}}.dump();
        }
        long window =
            req.arg2.empty() ? 3600 : std::strtol(req.arg2.c_str(), nullptr, 10);
        if (window <= 0) window = 3600;
        const std::int64_t since =
            static_cast<std::int64_t>(std::time(nullptr)) - window;
        json points = json::array();
        try {
            for (const auto& [ts, value] : db_->metric_history(req.arg, since)) {
                points.push_back({{"t", ts}, {"v", value}});
            }
        } catch (const DbError& e) {
            return json{{"ok", false}, {"error", e.what()}}.dump();
        }
        return json{{"ok", true}, {"cmd", "history"}, {"metric", req.arg},
                    {"points", points}}
            .dump();
    }

    if (req.cmd == "runs") {
        if (!db_) {
            return json{{"ok", false}, {"error", "runs unavailable"}}.dump();
        }
        if (req.arg.empty()) {
            return json{{"ok", false}, {"error", "missing job name"}}.dump();
        }
        int limit =
            req.arg2.empty()
                ? 20
                : static_cast<int>(std::strtol(req.arg2.c_str(), nullptr, 10));
        if (limit <= 0) limit = 20;
        json arr = json::array();
        try {
            for (const auto& r : db_->recent_runs(req.arg, limit)) {
                arr.push_back({
                    {"started_at", r.started_at},
                    {"ended_at", r.ended_at},
                    {"status", r.status},
                    {"exit_code", r.exit_code},
                    {"duration_ms", r.duration_ms},
                    {"trigger", r.trigger},
                });
            }
        } catch (const DbError& e) {
            return json{{"ok", false}, {"error", e.what()}}.dump();
        }
        return json{{"ok", true}, {"cmd", "runs"}, {"job", req.arg},
                    {"runs", arr}}
            .dump();
    }

    if (req.cmd == "add_job") {
        if (!add_job_handler_) {
            return json{{"ok", false}, {"error", "job control unavailable"}}
                .dump();
        }
        auto body = json::parse(request, nullptr, /*allow_exceptions=*/false);
        if (!body.is_object()) {
            return json{{"ok", false}, {"error", "add_job requires a JSON body"}}
                .dump();
        }
        auto str = [&](const char* k) -> std::string {
            return body.contains(k) && body[k].is_string()
                       ? body[k].get<std::string>()
                       : std::string();
        };
        auto num = [&](const char* k) -> int {
            return body.contains(k) && body[k].is_number()
                       ? body[k].get<int>()
                       : 0;
        };

        Job job;
        job.name = str("name");
        job.command = str("command");
        job.schedule_expr = str("schedule");
        job.timeout_seconds = num("timeout_seconds");
        job.retries = num("retries");
        if (body.contains("missed_run_policy")) {
            job.missed_run_policy =
                missed_run_policy_from_string(str("missed_run_policy"));
        }
        if (job.name.empty() || job.command.empty()) {
            return json{{"ok", false},
                        {"error", "name and command are required"}}
                .dump();
        }
        if (!job.schedule_expr.empty()) {
            auto cron = CronSchedule::parse(job.schedule_expr);
            if (!cron) {
                return json{{"ok", false},
                            {"error", "invalid cron schedule: '" +
                                          job.schedule_expr + "'"}}
                    .dump();
            }
            job.schedule = std::move(cron);
        }

        std::string err = add_job_handler_(job);
        if (!err.empty()) {
            return json{{"ok", false}, {"error", err}}.dump();
        }
        return json{{"ok", true}, {"cmd", "add_job"}, {"job", job.name}}.dump();
    }

    if (req.cmd == "remove_job") {
        if (!remove_job_handler_) {
            return json{{"ok", false}, {"error", "job control unavailable"}}
                .dump();
        }
        if (req.arg.empty()) {
            return json{{"ok", false}, {"error", "missing job name"}}.dump();
        }
        std::string err = remove_job_handler_(req.arg);
        if (!err.empty()) {
            return json{{"ok", false}, {"error", err}}.dump();
        }
        return json{{"ok", true}, {"cmd", "remove_job"}, {"job", req.arg}}
            .dump();
    }

    if (req.cmd == "run") {
        if (!run_handler_) {
            return json{{"ok", false}, {"error", "job control unavailable"}}
                .dump();
        }
        if (req.arg.empty()) {
            return json{{"ok", false}, {"error", "missing job name"}}.dump();
        }
        const bool queued = run_handler_(req.arg);
        if (!queued) {
            return json{{"ok", false},
                        {"error", "no such job"},
                        {"job", req.arg}}
                .dump();
        }
        return json{{"ok", true}, {"cmd", "run"}, {"job", req.arg},
                    {"queued", true}}
            .dump();
    }

    return json{{"ok", false}, {"error", "unknown command"}, {"cmd", req.cmd}}
        .dump();
}

}  // namespace agentpulse
