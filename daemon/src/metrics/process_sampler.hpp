#pragma once

#include <cstdint>
#include <chrono>
#include <unordered_map>
#include <vector>

#include "shared_state.hpp"  // ProcInfo

namespace agentpulse {

// Samples per-process CPU and memory usage. CPU percentage is derived from the
// change in a process's accumulated task time between successive samples, so
// the first call reports 0% CPU (no prior baseline) but valid RSS.
//
// Not thread-safe: keep one sampler on the sampling thread.
class ProcessSampler {
public:
    // Returns the top `n` processes by CPU%, then RSS. Percentages can exceed
    // 100 for multi-threaded processes (matching Activity Monitor).
    std::vector<ProcInfo> top(int n);

private:
    std::unordered_map<int, std::uint64_t> prev_time_ns_;  // pid -> task time
    std::chrono::steady_clock::time_point prev_sample_{};
    bool have_prev_ = false;
};

}  // namespace agentpulse
