#pragma once

#include <cstdint>
#include <string>

namespace agentpulse {

struct DiskSample {
    bool valid = false;
    std::uint64_t total_bytes = 0;
    std::uint64_t available_bytes = 0;  // available to a non-root user
    double used_percent = 0.0;
};

// Reads filesystem usage for the volume containing `path` via statfs.
DiskSample sample_disk(const std::string& path = "/");

}  // namespace agentpulse
