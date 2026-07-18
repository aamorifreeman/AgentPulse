#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/config.hpp"

namespace agentpulse {

// An alert transition emitted by the engine.
struct AlertEvent {
    std::string rule_name;
    std::string severity;    // warning/serious/critical
    std::string metric;
    std::string kind;        // "firing" or "recovered"
    double value = 0.0;
    double threshold = 0.0;
    std::int64_t ts = 0;
    std::string message;
    std::string attribution;  // optional, e.g. top CPU process
    bool notify = true;       // false if suppressed by quiet hours
};

// Evaluates alert rules against metric snapshots. Time (`now`) and metric
// values are passed in, so the engine is pure and deterministic — its
// duration/cooldown/recovery behavior is unit-testable without real clocks.
//
// State per rule persists across evaluate() calls: when the condition first
// became true (duration gate), whether it is currently firing (recovery),
// and when it last fired (cooldown).
class AlertEngine {
public:
    AlertEngine(std::vector<Rule> rules, QuietHours quiet_hours);

    // Metric values keyed by rule metric name (e.g. "system.cpu.percent").
    using MetricMap = std::unordered_map<std::string, double>;

    // Evaluates all rules and returns any firing/recovery transitions.
    // `attribution` is an optional human string attached to firing events
    // (e.g. the top CPU process), used when a rule's metric is CPU.
    std::vector<AlertEvent> evaluate(const MetricMap& metrics,
                                     std::int64_t now,
                                     const std::string& cpu_attribution = "");

    // True if `now` (unix seconds) falls within configured quiet hours.
    bool in_quiet_hours(std::int64_t now) const;

private:
    struct RuleState {
        std::int64_t since = 0;         // when condition first held (0 = not)
        bool firing = false;            // condition held past its duration
        bool notified = false;          // emitted a firing event this episode
        std::int64_t last_fired = 0;    // last firing timestamp
    };

    std::vector<Rule> rules_;
    QuietHours quiet_hours_;
    std::map<std::string, RuleState> state_;
};

// True if `value` satisfies `condition` against `threshold`.
bool condition_met(Condition condition, double value, double threshold);

}  // namespace agentpulse
