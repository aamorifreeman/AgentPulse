#pragma once

#include <array>
#include <ctime>
#include <optional>
#include <set>
#include <string>

namespace agentpulse {

// A parsed 5-field cron expression: "minute hour day-of-month month
// day-of-week". Each field supports '*', single values, comma lists, ranges
// (a-b), and steps (*/n, a-b/n). Day-of-week is 0-6 with 0 = Sunday (7 also
// accepted as Sunday).
class CronSchedule {
public:
    // Parses `expr`; returns nullopt if it is malformed.
    static std::optional<CronSchedule> parse(const std::string& expr);

    // True if the broken-down local time matches this schedule (second
    // component ignored).
    bool matches(const std::tm& tm) const;

    // Smallest minute-aligned time strictly after `after` that matches, or
    // nullopt if none within the search horizon (~4 years).
    std::optional<std::time_t> next_after(std::time_t after) const;

private:
    // Index order: minute, hour, day-of-month, month, day-of-week.
    std::array<std::set<int>, 5> fields_;
    bool dom_wildcard_ = true;  // day-of-month field was '*'
    bool dow_wildcard_ = true;  // day-of-week field was '*'
};

}  // namespace agentpulse
