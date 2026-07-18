#pragma once

#include <string>
#include <string_view>

namespace agentpulse {

enum class LogLevel { Debug, Info, Warn, Error };

// Minimum level that will be emitted. Defaults to Info.
void set_log_level(LogLevel level);

// Thread-safe. Writes a timestamped, level-tagged line to stderr
// (launchd redirects stderr to the daemon's log file).
void log_write(LogLevel level, std::string_view message);

// Convenience wrappers.
inline void log_debug(std::string_view m) { log_write(LogLevel::Debug, m); }
inline void log_info(std::string_view m)  { log_write(LogLevel::Info, m); }
inline void log_warn(std::string_view m)  { log_write(LogLevel::Warn, m); }
inline void log_error(std::string_view m) { log_write(LogLevel::Error, m); }

}  // namespace agentpulse
