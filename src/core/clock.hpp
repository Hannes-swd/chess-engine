#pragma once

#include "core/types.hpp"

#include <string>

namespace chess {

// Bedenkzeit: Grundzeit + Inkrement pro Zug (Fischer-Uhr). baseMs == 0 heisst "ohne Uhr".
struct TimeControl {
    int baseMs = 0;
    int incrementMs = 0;

    bool enabled() const { return baseMs > 0; }
    static TimeControl minutes(double minutes, int incrementSeconds);

    std::string label() const;   // "1+0", "3+2", "0.5+1", "Ohne Uhr"
    std::string pgnTag() const;  // "60+0" (Sekunden, PGN-Standard) oder "-"
};

// Reine Logik ohne eigene Zeitquelle: wer die Uhr benutzt, meldet verbrauchte Millisekunden.
class Clock {
public:
    struct Snapshot {
        int whiteMs = 0;
        int blackMs = 0;
    };

    Clock() = default;
    explicit Clock(TimeControl tc);

    const TimeControl& control() const { return tc_; }
    int remaining(Color c) const { return ms_[static_cast<int>(c)]; }
    void consume(Color c, int ms);
    void addIncrement(Color c);
    bool flagged(Color c) const { return tc_.enabled() && remaining(c) <= 0; }

    Snapshot snapshot() const { return {ms_[0], ms_[1]}; }
    void restore(const Snapshot& s);

    // "5:00", "1:02:03", unter 10 s mit Zehnteln: "0:09.4"
    static std::string format(int ms);

private:
    TimeControl tc_;
    int ms_[2] = {0, 0};
};

}  // namespace chess
