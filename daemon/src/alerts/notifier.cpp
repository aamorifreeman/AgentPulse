#include "alerts/notifier.hpp"

#include <spawn.h>
#include <sys/wait.h>

#include <string>

#include "log.hpp"

extern char** environ;

namespace agentpulse {

namespace {

// Escapes a string for embedding inside an AppleScript double-quoted literal.
std::string escape_applescript(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (c == '\\' || c == '"') out.push_back('\\');
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace

void send_notification(const std::string& title, const std::string& message) {
    const std::string script = "display notification \"" +
                               escape_applescript(message) +
                               "\" with title \"" + escape_applescript(title) +
                               "\"";

    const char* argv[] = {"osascript", "-e", script.c_str(), nullptr};
    pid_t pid = 0;
    int rc = posix_spawn(&pid, "/usr/bin/osascript", nullptr, nullptr,
                         const_cast<char* const*>(argv), environ);
    if (rc != 0) {
        log_warn("notification spawn failed (rc=" + std::to_string(rc) + ")");
        return;
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
}

}  // namespace agentpulse
