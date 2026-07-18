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

// A comparison operator for an alert rule.
enum class Condition { GreaterThan, LessThan, AtLeast };

std::string to_string(Condition c);

// An alert rule evaluated against a single metric value. Thresholds are
// numeric; the thermal state is mapped to its ordinal (nominal=0..critical=3)
// at load time so the engine only ever compares doubles.
struct Rule {
    std::string name;
    std::string metric;          // e.g. system.cpu.percent
    Condition condition = Condition::GreaterThan;
    double threshold = 0.0;
    int duration_seconds = 0;    // must hold this long before firing
    int cooldown_seconds = 0;    // minimum gap between fires
    std::string severity = "warning";  // warning/serious/critical
};

// Optional window during which non-critical notifications are held back.
struct QuietHours {
    bool enabled = false;
    int start_hour = 0;  // local hour [0,23], inclusive
    int end_hour = 0;    // local hour [0,23], exclusive; wraps past midnight
};

struct Config {
    std::vector<Job> jobs;
    std::vector<Rule> rules;
    QuietHours quiet_hours;
};

// Loads and validates the config file. A missing file yields an empty config
// (the daemon still runs system monitoring). Throws std::runtime_error with a
// human-readable message on malformed YAML or invalid job definitions.
Config load_config(const std::string& path);

}  // namespace agentpulse
