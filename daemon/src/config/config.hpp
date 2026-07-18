#pragma once

#include <optional>
#include <string>
#include <vector>

#include "schedule/cron.hpp"

namespace agentpulse {

// What to do about a scheduled run that was skipped while the Mac was asleep.
// Enforcement lands in M5; the field is parsed now so configs are stable.
enum class MissedRunPolicy {
    None,       // ignore missed runs
    RunOnWake,  // run once when the machine wakes
    RunNow,     // run immediately on detection
};

std::string to_string(MissedRunPolicy policy);

// A monitored automation, loaded from config.yaml.
struct Job {
    std::string name;
    std::string command;  // executed via `/bin/sh -c`
    std::optional<CronSchedule> schedule;
    std::string schedule_expr;  // original text, for display/logging
    MissedRunPolicy missed_run_policy = MissedRunPolicy::None;
    int timeout_seconds = 0;  // 0 = no timeout
    int retries = 0;          // additional attempts on failure (M5)
};

struct Config {
    std::vector<Job> jobs;
};

// Loads and validates the config file. A missing file yields an empty config
// (the daemon still runs system monitoring). Throws std::runtime_error with a
// human-readable message on malformed YAML or invalid job definitions.
Config load_config(const std::string& path);

}  // namespace agentpulse
