#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "config/config.hpp"
#include "db.hpp"

namespace agentpulse {

class SharedState;

// Owns the job set, fires due scheduled jobs, and runs manual "run now"
// requests. Each execution happens on its own worker thread (at most one
// concurrent run per job); results are persisted and published to SharedState.
//
// The scheduler holds its own Database connection and serializes all writes to
// it through a mutex, so it never shares a connection with the sampler thread.
class Scheduler {
public:
    Scheduler(std::vector<Job> jobs, const std::string& db_path,
              SharedState& state);
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // Starts the scheduler loop thread.
    void start();

    // Stops the loop and joins all in-flight worker threads.
    void stop();

    // Requests an immediate run of `name`. Returns false if no such job.
    bool request_run(const std::string& name);

private:
    struct RunningTask {
        std::thread thread;
        std::atomic<bool> done{false};
    };

    void loop();
    void launch(const Job& job, const std::string& trigger);
    void worker(Job job, std::string trigger, RunningTask* task);
    void reap_finished();          // join tasks that have completed
    void publish_status();         // refresh SharedState job snapshot

    const Job* find_job(const std::string& name) const;

    std::vector<Job> jobs_;
    std::string db_path_;
    SharedState& state_;

    std::unique_ptr<Database> db_;
    std::mutex db_mutex_;          // serializes writes to db_

    std::mutex mutex_;             // guards the maps below
    std::map<std::string, std::int64_t> next_run_;   // job -> next fire time
    std::map<std::string, std::unique_ptr<RunningTask>> running_;
    std::map<std::string, RunRecord> last_runs_;
    std::vector<std::string> manual_queue_;

    std::atomic<bool> active_{false};
    std::thread loop_thread_;
};

}  // namespace agentpulse
