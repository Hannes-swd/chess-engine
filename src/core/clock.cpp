#include "core/clock.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chess {

TimeControl TimeControl::minutes(double minutes, int incrementSeconds) {
    TimeControl tc;
    tc.baseMs = static_cast<int>(std::lround(minutes * 60000.0));
    tc.incrementMs = std::max(0, incrementSeconds) * 1000;
    return tc;
}

std::string TimeControl::label() const {
    if (!enabled()) return "Ohne Uhr";
    char buf[32];
    if (baseMs % 60000 == 0)
        std::snprintf(buf, sizeof(buf), "%d+%d", baseMs / 60000, incrementMs / 1000);
    else
        std::snprintf(buf, sizeof(buf), "%.1f+%d", baseMs / 60000.0, incrementMs / 1000);
    return buf;
}

std::string TimeControl::pgnTag() const {
    if (!enabled()) return "-";
    return std::to_string(baseMs / 1000) + "+" + std::to_string(incrementMs / 1000);
}

Clock::Clock(TimeControl tc) : tc_(tc) { ms_[0] = ms_[1] = tc.baseMs; }

void Clock::consume(Color c, int ms) {
    if (!tc_.enabled() || ms <= 0) return;
    int& r = ms_[static_cast<int>(c)];
    r = std::max(0, r - ms);
}

void Clock::addIncrement(Color c) {
    if (tc_.enabled()) ms_[static_cast<int>(c)] += tc_.incrementMs;
}

void Clock::restore(const Snapshot& s) {
    ms_[0] = s.whiteMs;
    ms_[1] = s.blackMs;
}

std::string Clock::format(int ms) {
    ms = std::max(0, ms);
    char buf[32];
    if (ms < 10000) {
        std::snprintf(buf, sizeof(buf), "0:%02d.%d", ms / 1000, (ms % 1000) / 100);
    } else {
        const int total = ms / 1000;
        const int h = total / 3600, m = (total / 60) % 60, s = total % 60;
        if (h > 0)
            std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
        else
            std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    }
    return buf;
}

}  // namespace chess
