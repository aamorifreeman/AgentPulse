#include "jobs/scheduler.hpp"

#include <chrono>
#include <ctime>
#include <utility>

#include "jobs/process_runner.hpp"
#include "log.hpp"
#include "shared_state.hpp"

namespace agentpulse {

namespace {
std::int64_t now_unix() { return std::time(nullptr); }
}  // namespace

std::optional<std::time_t> detect_missed_run(const CronSchedule& schedule,
                                             std::int64_t last_run_ts,
                                             std::int64_t now) {
    auto prev = schedule.prev_before(static_cast<std::time_t>(now));
    if (!prev) return std::nullopt;
    if (last_run_ts >= static_cast<std::int64_t>(*prev)) {
        return std::nullopt;  // already ran at/after that occurrence
    }
    return prev;
}

Scheduler::Scheduler(std::vector<Job> jobs, const std::string& db_path,
                     SharedState& state)
    : jobs_(std::move(jobs)), db_path_(db_path), state_(state) {
    db_ = std::make_unique<Database>(db_path_);
    migrate(*db_);  // idempotent; ensures the runs table exists

    const std::int64_t now = now_unix();
    for (const auto& job : jobs_) {
        if (job.schedule) {
            if (auto next = job.schedule->next_after(now)) {
                next_run_[job.name] = *next;
            }
        }
        if (auto last = db_->last_run(job.name)) {
            last_runs_[job.name] = std::move(*last);
        }
    }
    publish_status();
}

Scheduler::~Scheduler() { stop(); }

const Job* Scheduler::find_job(const std::string& name) const {
    for (const auto& job : jobs_) {
        if (job.name == name) return &job;
    }
    return nullptr;
}

void Scheduler::start() {
    if (active_.exchange(true)) return;
    last_wall_ = now_unix();
    loop_thread_ = std::thread(&Scheduler::loop, this);
    log_info("scheduler started with " + std::to_string(jobs_.size()) +
             " job(s)");
    scan_missed();  // catch runs skipped while the daemon was down/asleep
}

void Scheduler::stop() {
    if (!active_.exchange(false)) return;
    if (loop_thread_.joinable()) loop_thread_.join();

    // Join any in-flight worker threads (bounded by their timeouts).
    std::vector<std::unique_ptr<RunningTask>> tasks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, task] : running_) {
            tasks.push_back(std::move(task));
        }
        running_.clear();
    }
    for (auto& task : tasks) {
        if (task->thread.joinable()) task->thread.join();
    }
}

bool Scheduler::request_run(const std::string& name) {
    if (find_job(name) == nullptr) return false;
    enqueue(name, "manual");
    return true;
}

void Scheduler::notify_wake() { wake_pending_.store(true); }

void Scheduler::enqueue(const std::string& name, const std::string& trigger) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.emplace_back(name, trigger);
}

void Scheduler::scan_missed() {
    const std::int64_t now = now_unix();
    for (const auto& job : jobs_) {
        if (!job.schedule) continue;
        if (job.missed_run_policy == MissedRunPolicy::None) continue;

        std::int64_t last_run_ts = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = last_runs_.find(job.name); it != last_runs_.end()) {
                last_run_ts = it->second.started_at;
            }
        }
        auto missed = detect_missed_run(*job.schedule, last_run_ts, now);
        if (!missed) continue;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& handled = missed_handled_[job.name];
            if (handled >= static_cast<std::int64_t>(*missed)) {
                continue;  // already queued this occurrence
            }
            handled = *missed;
        }
        log_info("missed run detected for '" + job.name + "' (scheduled " +
                 std::to_string(*missed) + ", policy " +
                 to_string(job.missed_run_policy) + ") — queuing");
        enqueue(job.name, "missed");
    }
}

void Scheduler::loop() {
    using namespace std::chrono_literals;
    // If wall-clock jumps forward far more than a tick, the machine likely
    // slept; re-scan for missed runs. (Belt-and-suspenders alongside the IOKit
    // power monitor, and works without any entitlements.)
    constexpr std::int64_t kWakeGapSeconds = 90;

    while (active_.load()) {
        reap_finished();

        const std::int64_t wall = now_unix();
        const bool gap_wake = (wall - last_wall_) > kWakeGapSeconds;
        last_wall_ = wall;
        if (gap_wake || wake_pending_.exchange(false)) {
            log_info("wake detected — scanning for missed runs");
            scan_missed();
        }

        const std::int64_t now = wall;
        std::vector<std::pair<Job, std::string>> to_launch;
        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto is_running = [&](const std::string& n) {
                auto it = running_.find(n);
                return it != running_.end() && !it->second->done.load();
            };

            // Queued (manual/missed) requests first; defer any still running.
            std::vector<std::pair<std::string, std::string>> deferred;
            for (const auto& [name, trigger] : queue_) {
                const Job* job = find_job(name);
                if (job == nullptr) continue;
                if (is_running(name)) {
                    deferred.emplace_back(name, trigger);
                } else {
                    to_launch.emplace_back(*job, trigger);
                }
            }
            queue_ = std::move(deferred);

            // Due scheduled jobs.
            for (const auto& job : jobs_) {
                if (!job.schedule) continue;
                auto it = next_run_.find(job.name);
                if (it == next_run_.end() || it->second > now) continue;

                // Advance to the next occurrence so we don't refire this one.
                if (auto next = job.schedule->next_after(now)) {
                    it->second = *next;
                } else {
                    next_run_.erase(it);
                }
                if (!is_running(job.name)) {
                    to_launch.emplace_back(job, "schedule");
                }
            }
        }

        for (auto& [job, trigger] : to_launch) {
            launch(job, trigger);
        }
        publish_status();

        std::this_thread::sleep_for(500ms);
    }
}

void Scheduler::launch(const Job& job, const std::string& trigger) {
    log_info("running job '" + job.name + "' (" + trigger + ")");
    auto task = std::make_unique<RunningTask>();
    RunningTask* tp = task.get();
    Job job_copy = job;
    std::string trig = trigger;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_[job.name] = std::move(task);
    }
    tp->thread = std::thread([this, job_copy = std::move(job_copy),
                              trig = std::move(trig), tp]() mutable {
        worker(std::move(job_copy), std::move(trig), tp);
    });
}

void Scheduler::worker(Job job, std::string trigger, RunningTask* task) {
    const int attempts = job.retries + 1;
    RunResult res;

    for (int attempt = 1; attempt <= attempts && active_.load(); ++attempt) {
        res = run_command(job.command, job.timeout_seconds);

        RunRecord rec;
        rec.job_name = job.name;
        rec.started_at = res.started_at;
        rec.ended_at = res.ended_at;
        rec.status = to_string(res.status);
        rec.exit_code = res.exit_code;
        rec.duration_ms = res.duration_ms;
        rec.stdout_text = res.stdout_text;
        rec.stderr_text = res.stderr_text;
        rec.trigger = trigger;

        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            try {
                db_->insert_run(rec);
            } catch (const DbError& e) {
                log_warn(std::string("insert_run failed: ") + e.what());
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_runs_[job.name] = std::move(rec);
        }

        log_info("job '" + job.name + "' finished: " + to_string(res.status) +
                 " (exit " + std::to_string(res.exit_code) + ", " +
                 std::to_string(res.duration_ms) + "ms, attempt " +
                 std::to_string(attempt) + "/" + std::to_string(attempts) +
                 ")");

        if (res.status == RunStatus::Success) break;
        if (attempt < attempts) {
            // Linear backoff (attempt seconds), interruptible on shutdown.
            const int backoff_s = attempt;
            log_info("job '" + job.name + "' failed; retrying in " +
                     std::to_string(backoff_s) + "s");
            for (int i = 0; i < backoff_s * 10 && active_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    // Signal completion last so the reaper only joins a returning thread.
    task->done.store(true);
}

void Scheduler::reap_finished() {
    std::vector<std::unique_ptr<RunningTask>> finished;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = running_.begin(); it != running_.end();) {
            if (it->second->done.load()) {
                finished.push_back(std::move(it->second));
                it = running_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& task : finished) {
        if (task->thread.joinable()) task->thread.join();
    }
}

void Scheduler::publish_status() {
    std::vector<JobStatus> statuses;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& job : jobs_) {
            JobStatus s;
            s.name = job.name;
            s.schedule_expr = job.schedule_expr;

            if (auto it = next_run_.find(job.name); it != next_run_.end()) {
                s.next_run = it->second;
            }
            if (auto it = running_.find(job.name);
                it != running_.end() && !it->second->done.load()) {
                s.running = true;
            }
            if (auto it = last_runs_.find(job.name); it != last_runs_.end()) {
                const RunRecord& r = it->second;
                s.has_last_run = true;
                s.last_status = r.status;
                s.last_started_at = r.started_at;
                s.last_exit_code = r.exit_code;
                s.last_duration_ms = r.duration_ms;
                s.last_trigger = r.trigger;
            }
            statuses.push_back(std::move(s));
        }
    }
    state_.set_jobs(std::move(statuses));
}

}  // namespace agentpulse
