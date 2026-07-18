#pragma once

#include <cstdint>

namespace agentpulse {

struct SelfStats {
    bool valid = false;
    std::uint64_t rss_bytes = 0;
    double cpu_percent = 0.0;  // this process, since the previous sample
};

// Measures the daemon's own resource footprint via proc_pidinfo(getpid()).
// CPU% is derived from the change in task time between calls, so the first
// call reports 0% CPU but valid RSS.
class SelfSampler {
public:
    SelfStats sample();

private:
    bool have_prev_ = false;
    std::uint64_t prev_time_ns_ = 0;
    std::int64_t prev_wall_ns_ = 0;
};

}  // namespace agentpulse
