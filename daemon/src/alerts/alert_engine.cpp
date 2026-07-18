#include "alerts/alert_engine.hpp"

#include <ctime>
#include <sstream>
#include <utility>

namespace agentpulse {

bool condition_met(Condition condition, double value, double threshold) {
    switch (condition) {
        case Condition::GreaterThan: return value > threshold;
        case Condition::LessThan:    return value < threshold;
        case Condition::AtLeast:     return value >= threshold;
    }
    return false;
}

AlertEngine::AlertEngine(std::vector<Rule> rules, QuietHours quiet_hours)
    : rules_(std::move(rules)), quiet_hours_(quiet_hours) {}

bool AlertEngine::in_quiet_hours(std::int64_t now) const {
    if (!quiet_hours_.enabled) return false;
    std::time_t t = static_cast<std::time_t>(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    const int h = tm.tm_hour;
    const int start = quiet_hours_.start_hour;
    const int end = quiet_hours_.end_hour;
    if (start == end) return false;
    if (start < end) {
        return h >= start && h < end;
    }
    // Wraps past midnight (e.g. 22 -> 7).
    return h >= start || h < end;
}

namespace {

std::string format_message(const Rule& rule, double value, bool firing) {
    std::ostringstream os;
    if (firing) {
        os << rule.name << ": " << rule.metric << " = " << value
           << " (" << to_string(rule.condition) << " " << rule.threshold << ")";
    } else {
        os << rule.name << ": recovered (" << rule.metric << " = " << value
           << ")";
    }
    return os.str();
}

}  // namespace

std::vector<AlertEvent> AlertEngine::evaluate(
    const MetricMap& metrics, std::int64_t now,
    const std::string& cpu_attribution) {
    std::vector<AlertEvent> events;
    const bool quiet = in_quiet_hours(now);

    for (const auto& rule : rules_) {
        RuleState& st = state_[rule.name];

        auto it = metrics.find(rule.metric);
        const bool have_value = it != metrics.end();
        const double value = have_value ? it->second : 0.0;
        const bool met = have_value &&
                         condition_met(rule.condition, value, rule.threshold);

        if (met) {
            if (st.since == 0) st.since = now;
            const bool duration_ok =
                (now - st.since) >= rule.duration_seconds;

            if (duration_ok) {
                st.firing = true;
                // Emit a firing event once the cooldown allows, even if the
                // duration was met on an earlier tick (cooldown gates the
                // notification, not the firing state itself).
                if (!st.notified) {
                    const bool cooldown_ok =
                        st.last_fired == 0 ||
                        (now - st.last_fired) >= rule.cooldown_seconds;
                    if (cooldown_ok) {
                        st.notified = true;
                        st.last_fired = now;
                        AlertEvent e;
                        e.rule_name = rule.name;
                        e.severity = rule.severity;
                        e.metric = rule.metric;
                        e.kind = "firing";
                        e.value = value;
                        e.threshold = rule.threshold;
                        e.ts = now;
                        e.message = format_message(rule, value, /*firing=*/true);
                        if (rule.metric == "system.cpu.percent") {
                            e.attribution = cpu_attribution;
                        }
                        // Critical alerts always notify; others are held in
                        // quiet hours (still recorded, just no push).
                        e.notify = (rule.severity == "critical") || !quiet;
                        events.push_back(std::move(e));
                    }
                }
            }
        } else {
            st.since = 0;
            st.firing = false;
            if (st.notified) {
                st.notified = false;
                AlertEvent e;
                e.rule_name = rule.name;
                e.severity = rule.severity;
                e.metric = rule.metric;
                e.kind = "recovered";
                e.value = value;
                e.threshold = rule.threshold;
                e.ts = now;
                e.message = format_message(rule, value, /*firing=*/false);
                e.notify = (rule.severity == "critical") || !quiet;
                events.push_back(std::move(e));
            }
        }
    }
    return events;
}

}  // namespace agentpulse
