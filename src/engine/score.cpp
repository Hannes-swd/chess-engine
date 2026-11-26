#include "engine/score.hpp"

#include <algorithm>
#include <cstdlib>

namespace chess {

Score Score::centipawns(int cp) {
    Score s;
    s.value = cp;
    return s;
}

Score Score::mateIn(int moves, bool winning) {
    Score s;
    s.mate = true;
    s.winning = winning;
    s.value = moves;
    return s;
}

Score Score::negated() const {
    Score s = *this;
    if (mate)
        s.winning = !winning;
    else
        s.value = -value;
    return s;
}

long long Score::order() const {
    if (!mate) return value;
    return winning ? 1'000'000LL - value : -1'000'000LL + value;
}

int Score::clamped(int limit) const {
    if (mate) return winning ? limit : -limit;
    return std::clamp(value, -limit, limit);
}

namespace {

std::string pawns(int cp, bool plusSign) {
    const int a = std::abs(cp);
    std::string s;
    if (cp < 0)
        s += '-';
    else if (plusSign)
        s += '+';
    s += std::to_string(a / 100);
    s += '.';
    const int frac = a % 100;
    if (frac < 10) s += '0';
    s += std::to_string(frac);
    return s;
}

}  // namespace

std::string Score::toString() const {
    if (mate && value == 0) return "Matt";
    if (mate) return std::string("#") + (winning ? "" : "-") + std::to_string(value);
    return pawns(value, true);
}

std::string Score::toPgnEval() const {
    if (mate) return std::string("#") + (winning ? "" : "-") + std::to_string(value);
    return pawns(value, false);
}

}  // namespace chess
