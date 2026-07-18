#pragma once

#include <atomic>
#include <functional>
#include <thread>

#include <IOKit/pwr_mgt/IOPMLib.h>
#include <CoreFoundation/CoreFoundation.h>

namespace agentpulse {

// Watches macOS sleep/wake via IOKit and invokes a callback on wake. Runs a
// CFRunLoop on its own thread. Best-effort: if registration fails it logs and
// stays inert (the scheduler's wall-clock gap detection still covers wake).
class SleepMonitor {
public:
    using WakeCallback = std::function<void()>;

    SleepMonitor() = default;
    ~SleepMonitor();

    SleepMonitor(const SleepMonitor&) = delete;
    SleepMonitor& operator=(const SleepMonitor&) = delete;

    void start(WakeCallback on_wake);
    void stop();

private:
    static void power_callback(void* refcon, io_service_t service,
                               natural_t type, void* argument);

    WakeCallback on_wake_;
    io_connect_t root_port_ = MACH_PORT_NULL;
    IONotificationPortRef notify_port_ = nullptr;
    io_object_t notifier_ = 0;
    std::atomic<CFRunLoopRef> run_loop_{nullptr};
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace agentpulse
