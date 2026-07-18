#include "ipc/api.hpp"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

#include "shared_state.hpp"

namespace agentpulse {

using json = nlohmann::json;

namespace {

// Extracts the command name from a request line: the "cmd" field of a JSON
// object, or the trimmed line itself if it isn't such an object.
std::string parse_command(const std::string& request) {
    auto parsed = json::parse(request, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_object() && parsed.contains("cmd") &&
        parsed["cmd"].is_string()) {
        return parsed["cmd"].get<std::string>();
    }

    // Fall back to treating the whole line as a bare command word.
    std::string cmd = request;
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    cmd.erase(cmd.begin(), std::find_if(cmd.begin(), cmd.end(), not_space));
    cmd.erase(std::find_if(cmd.rbegin(), cmd.rend(), not_space).base(),
              cmd.end());
    return cmd;
}

}  // namespace

std::string Api::handle(const std::string& request) const {
    const std::string cmd = parse_command(request);

    if (cmd == "ping") {
        return json{{"ok", true}, {"cmd", "ping"}, {"reply", "pong"}}.dump();
    }

    if (cmd == "status") {
        const SharedState::Cpu cpu = state_.cpu();
        json j{
            {"ok", true},
            {"cmd", "status"},
            {"daemon", {{"started_at", started_at_}}},
            {"cpu",
             {{"valid", cpu.valid},
              {"percent", cpu.percent},
              {"sampled_at", cpu.sampled_at}}},
        };
        return j.dump();
    }

    return json{{"ok", false}, {"error", "unknown command"}, {"cmd", cmd}}
        .dump();
}

}  // namespace agentpulse
