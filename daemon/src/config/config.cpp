#include "config/config.hpp"

#include <filesystem>
#include <stdexcept>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

namespace agentpulse {

std::string to_string(MissedRunPolicy policy) {
    switch (policy) {
        case MissedRunPolicy::None:      return "none";
        case MissedRunPolicy::RunOnWake: return "run_on_wake";
        case MissedRunPolicy::RunNow:    return "run_now";
    }
    return "none";
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
    return config;
}

}  // namespace agentpulse
