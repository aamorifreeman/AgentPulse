#include "config/config.hpp"

#include <filesystem>
#include <stdexcept>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

#include "metrics/thermal_sampler.hpp"

namespace agentpulse {

std::string to_string(MissedRunPolicy policy) {
    switch (policy) {
        case MissedRunPolicy::None:      return "none";
        case MissedRunPolicy::RunOnWake: return "run_on_wake";
        case MissedRunPolicy::RunNow:    return "run_now";
    }
    return "none";
}

std::string to_string(Condition c) {
    switch (c) {
        case Condition::GreaterThan: return "greater_than";
        case Condition::LessThan:    return "less_than";
        case Condition::AtLeast:     return "at_least";
    }
    return "greater_than";
}

MissedRunPolicy missed_run_policy_from_string(const std::string& s) {
    if (s == "run_on_wake") return MissedRunPolicy::RunOnWake;
    if (s == "run_now") return MissedRunPolicy::RunNow;
    return MissedRunPolicy::None;  // lenient: unknown -> none
}

namespace {

MissedRunPolicy parse_policy(const std::string& s) {
    if (s == "run_on_wake") return MissedRunPolicy::RunOnWake;
    if (s == "run_now") return MissedRunPolicy::RunNow;
    if (s == "none" || s.empty()) return MissedRunPolicy::None;
    throw std::runtime_error("invalid missed_run_policy: '" + s + "'");
}

Job parse_job(const YAML::Node& node, std::size_t index) {
    if (!node.IsMap()) {
        throw std::runtime_error("job #" + std::to_string(index) +
                                 " is not a mapping");
    }

    Job job;
    if (!node["name"] || node["name"].as<std::string>().empty()) {
        throw std::runtime_error("job #" + std::to_string(index) +
                                 " is missing 'name'");
    }
    job.name = node["name"].as<std::string>();

    if (!node["command"] || node["command"].as<std::string>().empty()) {
        throw std::runtime_error("job '" + job.name + "' is missing 'command'");
    }
    job.command = node["command"].as<std::string>();

    if (node["schedule"]) {
        job.schedule_expr = node["schedule"].as<std::string>();
        auto cron = CronSchedule::parse(job.schedule_expr);
        if (!cron) {
            throw std::runtime_error("job '" + job.name +
                                     "' has invalid cron schedule: '" +
                                     job.schedule_expr + "'");
        }
        job.schedule = std::move(cron);
    }

    if (node["missed_run_policy"]) {
        job.missed_run_policy =
            parse_policy(node["missed_run_policy"].as<std::string>());
    }
    if (node["timeout_seconds"]) {
        job.timeout_seconds = node["timeout_seconds"].as<int>();
        if (job.timeout_seconds < 0) {
            throw std::runtime_error("job '" + job.name +
                                     "' has negative timeout_seconds");
        }
    }
    if (node["retries"]) {
        job.retries = node["retries"].as<int>();
        if (job.retries < 0) {
            throw std::runtime_error("job '" + job.name +
                                     "' has negative retries");
        }
    }
    return job;
}

Condition parse_condition(const std::string& s) {
    if (s == "greater_than" || s == ">") return Condition::GreaterThan;
    if (s == "less_than" || s == "<") return Condition::LessThan;
    if (s == "at_least" || s == ">=") return Condition::AtLeast;
    throw std::runtime_error("invalid condition: '" + s + "'");
}

Rule parse_rule(const YAML::Node& node, std::size_t index) {
    if (!node.IsMap()) {
        throw std::runtime_error("rule #" + std::to_string(index) +
                                 " is not a mapping");
    }
    Rule rule;
    if (!node["name"] || node["name"].as<std::string>().empty()) {
        throw std::runtime_error("rule #" + std::to_string(index) +
                                 " is missing 'name'");
    }
    rule.name = node["name"].as<std::string>();

    if (!node["metric"]) {
        throw std::runtime_error("rule '" + rule.name + "' is missing 'metric'");
    }
    rule.metric = node["metric"].as<std::string>();

    if (!node["condition"]) {
        throw std::runtime_error("rule '" + rule.name +
                                 "' is missing 'condition'");
    }
    rule.condition = parse_condition(node["condition"].as<std::string>());

    if (!node["threshold"]) {
        throw std::runtime_error("rule '" + rule.name +
                                 "' is missing 'threshold'");
    }
    // Thermal thresholds are given as labels; map to the ordinal so the engine
    // compares numbers uniformly.
    if (rule.metric == "system.thermal_state") {
        rule.threshold = static_cast<double>(
            thermal_state_from_string(node["threshold"].as<std::string>()));
    } else {
        rule.threshold = node["threshold"].as<double>();
    }

    if (node["duration_seconds"]) {
        rule.duration_seconds = node["duration_seconds"].as<int>();
    }
    if (node["cooldown_seconds"]) {
        rule.cooldown_seconds = node["cooldown_seconds"].as<int>();
    }
    if (node["severity"]) {
        rule.severity = node["severity"].as<std::string>();
    }
    if (rule.duration_seconds < 0 || rule.cooldown_seconds < 0) {
        throw std::runtime_error("rule '" + rule.name +
                                 "' has negative duration/cooldown");
    }
    return rule;
}

}  // namespace

Config load_config(const std::string& path) {
    Config config;
    if (!std::filesystem::exists(path)) {
        return config;  // no config yet — system monitoring only
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        throw std::runtime_error(std::string("cannot parse config: ") +
                                 e.what());
    }

    if (root["jobs"]) {
        if (!root["jobs"].IsSequence()) {
            throw std::runtime_error("'jobs' must be a sequence");
        }
        std::unordered_set<std::string> names;
        std::size_t i = 0;
        for (const auto& node : root["jobs"]) {
            Job job = parse_job(node, i++);
            if (!names.insert(job.name).second) {
                throw std::runtime_error("duplicate job name: '" + job.name +
                                         "'");
            }
            config.jobs.push_back(std::move(job));
        }
    }

    if (root["rules"]) {
        if (!root["rules"].IsSequence()) {
            throw std::runtime_error("'rules' must be a sequence");
        }
        std::unordered_set<std::string> names;
        std::size_t i = 0;
        for (const auto& node : root["rules"]) {
            Rule rule = parse_rule(node, i++);
            if (!names.insert(rule.name).second) {
                throw std::runtime_error("duplicate rule name: '" + rule.name +
                                         "'");
            }
            config.rules.push_back(std::move(rule));
        }
    }

    if (root["quiet_hours"]) {
        const auto& qh = root["quiet_hours"];
        config.quiet_hours.enabled = true;
        if (qh["start_hour"])
            config.quiet_hours.start_hour = qh["start_hour"].as<int>();
        if (qh["end_hour"])
            config.quiet_hours.end_hour = qh["end_hour"].as<int>();
    }
    return config;
}

}  // namespace agentpulse
