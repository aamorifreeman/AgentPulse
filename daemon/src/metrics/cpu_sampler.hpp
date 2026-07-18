#pragma once

namespace agentpulse {

// Computes overall CPU utilization by differencing the host's cumulative
// CPU tick counters between successive calls to sample().
//
// macOS exposes cumulative ticks (user/system/idle/nice) via the Mach
// host_statistics API. A single reading is meaningless; utilization is the
// ratio of busy-tick delta to total-tick delta across an interval.
class CpuSampler {
public:
    CpuSampler();

    // Returns overall CPU busy percentage [0, 100] since the previous call.
    // The first call establishes a baseline and returns 0.
    double sample();

private:
    bool have_prev_ = false;
    unsigned long long prev_user_ = 0;
    unsigned long long prev_system_ = 0;
    unsigned long long prev_idle_ = 0;
    unsigned long long prev_nice_ = 0;
};

}  // namespace agentpulse
