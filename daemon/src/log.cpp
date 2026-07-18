#include "log.hpp"

#include <array>
#include <ctime>
#include <cstdio>
#include <mutex>

namespace agentpulse {
namespace {

std::mutex g_log_mutex;
LogLevel g_min_level = LogLevel::Info;

const char* level_tag(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

}  // namespace

void set_log_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_min_level = level;
}

void log_write(LogLevel level, std::string_view message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (level < g_min_level) {
        return;
    }

    std::timespec ts{};
    std::timespec_get(&ts, TIME_UTC);
    std::tm tm{};
    localtime_r(&ts.tv_sec, &tm);

    std::array<char, 32> stamp{};
    std::strftime(stamp.data(), stamp.size(), "%Y-%m-%d %H:%M:%S", &tm);

    std::fprintf(stderr, "%s.%03ld [%s] %.*s\n",
                 stamp.data(),
                 ts.tv_nsec / 1'000'000,
                 level_tag(level),
                 static_cast<int>(message.size()),
                 message.data());
    std::fflush(stderr);
}

}  // namespace agentpulse
