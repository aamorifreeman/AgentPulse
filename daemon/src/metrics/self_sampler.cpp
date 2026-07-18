#include "metrics/self_sampler.hpp"

#include <libproc.h>
#include <sys/proc_info.h>
#include <unistd.h>

#include <chrono>

namespace agentpulse {

SelfStats SelfSampler::sample() {
    SelfStats s;

    proc_taskinfo ti{};
    int rc = proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &ti, sizeof(ti));
    if (rc != static_cast<int>(sizeof(ti))) {
        return s;
    }

    s.rss_bytes = ti.pti_resident_size;
    s.valid = true;

    const std::uint64_t total_ns = ti.pti_total_user + ti.pti_total_system;
    const std::int64_t wall_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();

    if (have_prev_ && wall_ns > prev_wall_ns_ && total_ns >= prev_time_ns_) {
        const double interval = static_cast<double>(wall_ns - prev_wall_ns_);
        s.cpu_percent =
            (static_cast<double>(total_ns - prev_time_ns_) / interval) * 100.0;
    }

    prev_time_ns_ = total_ns;
    prev_wall_ns_ = wall_ns;
    have_prev_ = true;
    return s;
}

}  // namespace agentpulse
