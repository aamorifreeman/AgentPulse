#include "power/sleep_monitor.hpp"

#include <IOKit/IOMessage.h>

#include <chrono>

#include "log.hpp"

namespace agentpulse {

SleepMonitor::~SleepMonitor() { stop(); }

void SleepMonitor::power_callback(void* refcon, io_service_t /*service*/,
                                  natural_t type, void* argument) {
    auto* self = static_cast<SleepMonitor*>(refcon);
    switch (type) {
        case kIOMessageCanSystemSleep:
            // Allow idle sleep (we don't veto).
            IOAllowPowerChange(self->root_port_,
                               reinterpret_cast<long>(argument));
            break;
        case kIOMessageSystemWillSleep:
            log_info("system going to sleep");
            IOAllowPowerChange(self->root_port_,
                               reinterpret_cast<long>(argument));
            break;
        case kIOMessageSystemHasPoweredOn:
            log_info("system woke from sleep");
            if (self->on_wake_) self->on_wake_();
            break;
        default:
            break;
    }
}

void SleepMonitor::start(WakeCallback on_wake) {
    if (running_.exchange(true)) return;
    on_wake_ = std::move(on_wake);

    thread_ = std::thread([this] {
        root_port_ = IORegisterForSystemPower(this, &notify_port_,
                                              power_callback, &notifier_);
        if (root_port_ == MACH_PORT_NULL) {
            log_warn("IORegisterForSystemPower failed; sleep monitor inert");
            running_.store(false);
            return;
        }
        CFRunLoopRef loop = CFRunLoopGetCurrent();
        CFRunLoopAddSource(loop,
                           IONotificationPortGetRunLoopSource(notify_port_),
                           kCFRunLoopCommonModes);
        run_loop_.store(loop);
        CFRunLoopRun();

        // Torn down after CFRunLoopStop().
        CFRunLoopRemoveSource(loop,
                              IONotificationPortGetRunLoopSource(notify_port_),
                              kCFRunLoopCommonModes);
        IODeregisterForSystemPower(&notifier_);
        IOServiceClose(root_port_);
        IONotificationPortDestroy(notify_port_);
        root_port_ = MACH_PORT_NULL;
    });
}

void SleepMonitor::stop() {
    if (!running_.exchange(false)) return;

    // Wait briefly for the run loop to come up, then stop it.
    for (int i = 0; i < 50 && run_loop_.load() == nullptr; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (CFRunLoopRef loop = run_loop_.load()) {
        CFRunLoopStop(loop);
    }
    if (thread_.joinable()) thread_.join();
    run_loop_.store(nullptr);
}

}  // namespace agentpulse
