#include "metrics/memory_sampler.hpp"

#include <mach/mach.h>
#include <sys/sysctl.h>

#include "log.hpp"

namespace agentpulse {

MemorySample sample_memory() {
    MemorySample s;

    std::uint64_t total = 0;
    std::size_t len = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &len, nullptr, 0) != 0 ||
        total == 0) {
        log_warn("sysctl hw.memsize failed");
        return s;
    }

    vm_size_t page_size = 0;
    if (host_page_size(mach_host_self(), &page_size) != KERN_SUCCESS) {
        log_warn("host_page_size failed");
        return s;
    }

    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm),
                          &count) != KERN_SUCCESS) {
        log_warn("host_statistics64(HOST_VM_INFO64) failed");
        return s;
    }

    const std::uint64_t used_pages = static_cast<std::uint64_t>(vm.active_count) +
                                     vm.wire_count + vm.compressor_page_count;
    s.total_bytes = total;
    s.used_bytes = used_pages * static_cast<std::uint64_t>(page_size);
    if (s.used_bytes > s.total_bytes) {
        s.used_bytes = s.total_bytes;
    }
    s.used_percent =
        (static_cast<double>(s.used_bytes) / static_cast<double>(total)) * 100.0;
    s.valid = true;
    return s;
}

}  // namespace agentpulse
