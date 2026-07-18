#include "metrics/cpu_sampler.hpp"

#include <mach/mach.h>

#include "log.hpp"

namespace agentpulse {

CpuSampler::CpuSampler() = default;

double CpuSampler::sample() {
    host_cpu_load_info_data_t info{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    kern_return_t kr = host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                                       reinterpret_cast<host_info_t>(&info),
                                       &count);
    if (kr != KERN_SUCCESS) {
        log_warn("host_statistics(HOST_CPU_LOAD_INFO) failed");
        return 0.0;
    }

    const unsigned long long user = info.cpu_ticks[CPU_STATE_USER];
    const unsigned long long system = info.cpu_ticks[CPU_STATE_SYSTEM];
    const unsigned long long idle = info.cpu_ticks[CPU_STATE_IDLE];
    const unsigned long long nice = info.cpu_ticks[CPU_STATE_NICE];

    if (!have_prev_) {
        prev_user_ = user;
        prev_system_ = system;
        prev_idle_ = idle;
        prev_nice_ = nice;
        have_prev_ = true;
        return 0.0;
    }

    const unsigned long long d_user = user - prev_user_;
    const unsigned long long d_system = system - prev_system_;
    const unsigned long long d_idle = idle - prev_idle_;
    const unsigned long long d_nice = nice - prev_nice_;

    prev_user_ = user;
    prev_system_ = system;
    prev_idle_ = idle;
    prev_nice_ = nice;

    const unsigned long long busy = d_user + d_system + d_nice;
    const unsigned long long total = busy + d_idle;
    if (total == 0) {
        return 0.0;
    }
    return (static_cast<double>(busy) / static_cast<double>(total)) * 100.0;
}

}  // namespace agentpulse
