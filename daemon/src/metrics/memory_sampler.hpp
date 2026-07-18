#pragma once

#include <cstdint>

namespace agentpulse {

struct MemorySample {
    bool valid = false;
    std::uint64_t total_bytes = 0;
    std::uint64_t used_bytes = 0;   // active + wired + compressed
    double used_percent = 0.0;
};

// Reads physical memory usage via sysctl(hw.memsize) and the Mach VM
// statistics. "Used" counts active, wired, and compressed pages — the
// resident, non-reclaimable footprint.
MemorySample sample_memory();

}  // namespace agentpulse
