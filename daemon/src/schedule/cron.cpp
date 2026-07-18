#include "schedule/cron.hpp"

#include <sstream>
#include <vector>

namespace agentpulse {

namespace {

struct FieldRange {
    int lo;
    int hi;
};

// Parses one cron field into the set of matching integers within [range.lo,
// range.hi]. Supports '*', 'a', 'a-b', 'a,b,c', '*/n', 'a-b/n'. Returns false
// on any malformed token.
bool parse_field(const std::string& field, FieldRange range,
                 std::set<int>& out, bool& is_wildcard) {
    is_wildcard = (field == "*");

    std::stringstream ss(field);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) {
            return false;
        }

        int step = 1;
        std::string base = token;
        if (auto slash = token.find('/'); slash != std::string::npos) {
            base = token.substr(0, slash);
            try {
                step = std::stoi(token.substr(slash + 1));
            } catch (...) {
                return false;
            }
            if (step <= 0) {
                return false;
            }
        }

        int lo = range.lo;
        int hi = range.hi;
        if (base == "*") {
            // full range with step
        } else if (auto dash = base.find('-'); dash != std::string::npos) {
            try {
                lo = std::stoi(base.substr(0, dash));
                hi = std::stoi(base.substr(dash + 1));
            } catch (...) {
                return false;
            }
        } else {
            try {
                lo = hi = std::stoi(base);
            } catch (...) {
                return false;
            }
        }

        if (lo < range.lo || hi > range.hi || lo > hi) {
            return false;
        }
        for (int v = lo; v <= hi; v += step) {
            out.insert(v);
        }
    }
    return !out.empty();
}

}  // namespace

std::optional<CronSchedule> CronSchedule::parse(const std::string& expr) {
    std::stringstream ss(expr);
    std::vector<std::string> parts;
    std::string p;
    while (ss >> p) {
        parts.push_back(p);
    }
    if (parts.size() != 5) {
        return std::nullopt;
    }

    static const FieldRange kRanges[5] = {
        {0, 59},  // minute
        {0, 23},  // hour
        {1, 31},  // day of month
        {1, 12},  // month
        {0, 7},   // day of week (0 and 7 both Sunday)
    };

    CronSchedule sched;
    for (int i = 0; i < 5; ++i) {
        bool wildcard = false;
        if (!parse_field(parts[i], kRanges[i], sched.fields_[i], wildcard)) {
            return std::nullopt;
        }
        if (i == 2) sched.dom_wildcard_ = wildcard;
        if (i == 4) sched.dow_wildcard_ = wildcard;
    }
    // Normalize Sunday: treat 7 as 0 so tm_wday (0-6) matches.
    if (sched.fields_[4].count(7)) {
        sched.fields_[4].insert(0);
    }
    return sched;
}

bool CronSchedule::matches(const std::tm& tm) const {
    if (!fields_[0].count(tm.tm_min)) return false;
    if (!fields_[1].count(tm.tm_hour)) return false;
    if (!fields_[3].count(tm.tm_mon + 1)) return false;

    const bool dom_ok = fields_[2].count(tm.tm_mday) > 0;
    const bool dow_ok = fields_[4].count(tm.tm_wday) > 0;

    // Standard cron rule: if both day fields are restricted, match on EITHER;
    // otherwise honor whichever is restricted.
    if (dom_wildcard_ && dow_wildcard_) return true;
    if (dom_wildcard_) return dow_ok;
    if (dow_wildcard_) return dom_ok;
    return dom_ok || dow_ok;
}

std::optional<std::time_t> CronSchedule::next_after(std::time_t after) const {
    // Advance to the next whole minute strictly after `after`.
    std::time_t t = after - (after % 60) + 60;

    // Search horizon: ~4 years of minutes guards against impossible dates
    // (e.g. Feb 30).
    constexpr long kMaxMinutes = 4L * 366 * 24 * 60;
    for (long i = 0; i < kMaxMinutes; ++i, t += 60) {
        std::tm tm{};
        localtime_r(&t, &tm);
        if (matches(tm)) {
            return t;
        }
    }
    return std::nullopt;
}

}  // namespace agentpulse
