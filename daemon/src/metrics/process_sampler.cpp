#include "metrics/process_sampler.hpp"

#include <libproc.h>
#include <sys/proc_info.h>

#include <algorithm>
#include <vector>

namespace agentpulse {

std::vector<ProcInfo> ProcessSampler::top(int n) {
    // Enumerate all pids.
    int needed = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::vector<pid_t> pids(needed / sizeof(pid_t) + 16, 0);
    int got = proc_listpids(PROC_ALL_PIDS, 0, pids.data(),
                            static_cast<int>(pids.size() * sizeof(pid_t)));
    if (got <= 0) {
        return {};
    }
    const std::size_t count = got / sizeof(pid_t);

    const auto now = std::chrono::steady_clock::now();
    double interval_ns = 0.0;
    if (have_prev_) {
        interval_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          now - prev_sample_)
                          .count();
    }

    std::unordered_map<int, std::uint64_t> cur_time_ns;
    cur_time_ns.reserve(count);

    std::vector<ProcInfo> procs;
    procs.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const int pid = pids[i];
        if (pid <= 0) continue;

        proc_taskinfo ti{};
        int rc = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &ti, sizeof(ti));
        if (rc != static_cast<int>(sizeof(ti))) {
            continue;  // pid gone or not permitted
        }

        const std::uint64_t total_ns = ti.pti_total_user + ti.pti_total_system;
        cur_time_ns[pid] = total_ns;

        ProcInfo info;
        info.pid = pid;
        info.rss_bytes = ti.pti_resident_size;

        char name[2 * MAXCOMLEN + 1] = {0};
        if (proc_name(pid, name, sizeof(name)) > 0) {
            info.name = name;
        } else {
            info.name = "pid " + std::to_string(pid);
        }

        if (have_prev_ && interval_ns > 0.0) {
            auto it = prev_time_ns_.find(pid);
            if (it != prev_time_ns_.end() && total_ns >= it->second) {
                info.cpu_percent =
                    (static_cast<double>(total_ns - it->second) / interval_ns) *
                    100.0;
            }
        }
        procs.push_back(std::move(info));
    }

    prev_time_ns_ = std::move(cur_time_ns);
    prev_sample_ = now;
    have_prev_ = true;

    if (n < 0) n = 0;
    const std::size_t keep = std::min<std::size_t>(procs.size(), n);
    std::partial_sort(
        procs.begin(), procs.begin() + keep, procs.end(),
        [](const ProcInfo& a, const ProcInfo& b) {
            if (a.cpu_percent != b.cpu_percent)
                return a.cpu_percent > b.cpu_percent;
            return a.rss_bytes > b.rss_bytes;
        });
    procs.resize(keep);
    return procs;
}

}  // namespace agentpulse
