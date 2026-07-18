#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace agentpulse {

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

private:
    mutable std::mutex mutex_;
    Cpu cpu_;
    std::vector<JobStatus> jobs_;
};

}  // namespace agentpulse

