#include "paths.hpp"

#include <cstdlib>
#include <stdexcept>

namespace agentpulse {
namespace fs = std::filesystem;

namespace {

fs::path home_dir() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        throw std::runtime_error("HOME environment variable is not set");
    }
    return fs::path(home);
}

void ensure_dir(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        throw std::runtime_error("failed to create directory " + dir.string() +
                                 ": " + ec.message());
    }
}

}  // namespace

fs::path Paths::database() const { return data_dir / "agentpulse.db"; }
fs::path Paths::socket() const { return data_dir / "agentpulse.sock"; }
fs::path Paths::config_file() const { return config_dir / "config.yaml"; }

Paths resolve_paths() {
    const fs::path home = home_dir();

    Paths p;
    p.data_dir = home / "Library" / "Application Support" / "AgentPulse";
    p.config_dir = home / ".config" / "agentpulse";
    p.log_dir = home / "Library" / "Logs" / "AgentPulse";

    ensure_dir(p.data_dir);
    ensure_dir(p.config_dir);
    ensure_dir(p.log_dir);

    return p;
}

}  // namespace agentpulse
