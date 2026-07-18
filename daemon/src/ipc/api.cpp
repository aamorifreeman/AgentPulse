#include "ipc/api.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <nlohmann/json.hpp>

#include "shared_state.hpp"

namespace agentpulse {

using json = nlohmann::json;

namespace {

struct Request {
    std::string cmd;
    std::string arg;  // e.g. job name for "run"
};

// Parses a request line: a JSON object {"cmd":..., "job":...} or a bare
// "cmd [arg]" token sequence.
Request parse_request(const std::string& line) {
    auto parsed = json::parse(line, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_object() && parsed.contains("cmd") &&
        parsed["cmd"].is_string()) {
        Request r;
        r.cmd = parsed["cmd"].get<std::string>();
        if (parsed.contains("job") && parsed["job"].is_string()) {
            r.arg = parsed["job"].get<std::string>();
        }
        return r;
    }

    Request r;
    std::istringstream ss(line);
    ss >> r.cmd >> r.arg;
    return r;
}

json job_to_json(const JobStatus& s) {
    json j{
        {"name", s.name},
        {"schedule", s.schedule_expr},
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

}  // namespace

std::string Api::handle(const std::string& request) const {
    const Request req = parse_request(request);

    if (req.cmd == "ping") {
        return json{{"ok", true}, {"cmd", "ping"}, {"reply", "pong"}}.dump();
    }

    if (req.cmd == "status") {
        const SharedState::Cpu cpu = state_.cpu();
        json j{
            {"ok", true},
            {"cmd", "status"},
            {"daemon", {{"started_at", started_at_}}},
            {"cpu",
             {{"valid", cpu.valid},
              {"percent", cpu.percent},
              {"sampled_at", cpu.sampled_at}}},
            {"jobs", jobs_array(state_)},
        };
        return j.dump();
    }

    if (req.cmd == "jobs") {
        return json{{"ok", true}, {"cmd", "jobs"}, {"jobs", jobs_array(state_)}}
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
