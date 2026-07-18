#include "metrics/disk_sampler.hpp"

#include <sys/mount.h>
#include <sys/param.h>

#include "log.hpp"

namespace agentpulse {

DiskSample sample_disk(const std::string& path) {
    DiskSample s;

    struct statfs st{};
    if (statfs(path.c_str(), &st) != 0) {
        log_warn("statfs(" + path + ") failed");
        return s;
    }

    const std::uint64_t bsize = st.f_bsize;
    s.total_bytes = static_cast<std::uint64_t>(st.f_blocks) * bsize;
    s.available_bytes = static_cast<std::uint64_t>(st.f_bavail) * bsize;
    if (s.total_bytes > 0) {
        const std::uint64_t used = s.total_bytes >= s.available_bytes
                                       ? s.total_bytes - s.available_bytes
                                       : 0;
        s.used_percent = (static_cast<double>(used) /
                          static_cast<double>(s.total_bytes)) *
                         100.0;
    }
    s.valid = true;
    return s;
}

}  // namespace agentpulse
