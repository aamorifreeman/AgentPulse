#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace agentpulse {

// A process's live resource usage, used for top-N lists and alert attribution.
struct ProcInfo {
    int pid = 0;
    std::string name;
    double cpu_percent = 0.0;  // may exceed 100 for multi-threaded processes
    std::uint64_t rss_bytes = 0;
};

// System health snapshot published by the sampler thread.
struct SystemSnapshot {
    bool valid = false;
    std::int64_t sampled_at = 0;

    std::uint64_t mem_total_bytes = 0;
    std::uint64_t mem_used_bytes = 0;
    double mem_used_percent = 0.0;

    std::uint64_t disk_total_bytes = 0;
    std::uint64_t disk_available_bytes = 0;
    double disk_used_percent = 0.0;

    std::string thermal_state;  // nominal/fair/serious/critical

    std::vector<ProcInfo> top_processes;  // highest CPU first
};

// Live status of one job, published by the scheduler for the socket API.
struct JobStatus {
    std::string name;
    std::string schedule_expr;   // empty if the job has no schedule
    std::int64_t next_run = 0;   // unix seconds, 0 if not scheduled
    bool running = false;

    bool has_last_run = false;
    std::string last_status;        // success/failed/timeout/spawn_error
    std::int64_t last_started_at = 0;
    int last_exit_code = 0;
    long last_duration_ms = 0;
    std::string last_trigger;
};

// Live snapshot of the most recent CPU sample. The sampler thread writes it;
// the socket thread reads it to answer "status" requests. History lives in
// SQLite; this is only the current value, kept in memory so the socket
// handler never touches the database (avoiding cross-thread connection use).
class SharedState {
public:
    struct Cpu {
        bool valid = false;
        double percent = 0.0;
        std::int64_t sampled_at = 0;  // unix seconds
    };

    void set_cpu(double percent, std::int64_t sampled_at) {
        std::lock_guard<std::mutex> lock(mutex_);
        cpu_ = {true, percent, sampled_at};
    }

    Cpu cpu() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cpu_;
    }

    void set_jobs(std::vector<JobStatus> jobs) {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_ = std::move(jobs);
    }

    std::vector<JobStatus> jobs() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return jobs_;
    }

    void set_system(SystemSnapshot system) {
        std::lock_guard<std::mutex> lock(mutex_);
        system_ = std::move(system);
    }

    SystemSnapshot system() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return system_;
    }

private:
    mutable std::mutex mutex_;
    Cpu cpu_;
    std::vector<JobStatus> jobs_;
    SystemSnapshot system_;
};

}  // namespace agentpulse

