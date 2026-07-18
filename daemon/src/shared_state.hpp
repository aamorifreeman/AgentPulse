#pragma once

#include <cstdint>
#include <mutex>

namespace agentpulse {

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

private:
    mutable std::mutex mutex_;
    Cpu cpu_;
};

}  // namespace agentpulse
