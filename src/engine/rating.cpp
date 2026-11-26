#include "engine/rating.hpp"

#include "util/json.hpp"

#include <algorithm>
#include <cmath>

namespace chess {

double expectedScore(int a, int b) { return 1.0 / (1.0 + std::pow(10.0, (b - a) / 400.0)); }

int eloDelta(int rating, int opponent, double score, int gamesPlayed) {
    const double k = gamesPlayed < 30 ? 40.0 : 20.0;
    return static_cast<int>(std::lround(k * (score - expectedScore(rating, opponent))));
}

int applyResult(PlayerProfile& p, int opponentElo, double score) {
    const int d = eloDelta(p.rating, opponentElo, score, p.games);
    p.rating = std::max(100, p.rating + d);
    p.peak = std::max(p.peak, p.rating);
    ++p.games;
    if (score > 0.75)
        ++p.wins;
    else if (score < 0.25)
        ++p.losses;
    else
        ++p.draws;
    p.lastDelta = d;
    return d;
}

int estimatedBotElo(const BotProfile& p, bool withStockfish) {
    double e;
    if (withStockfish && p.engine != EngineChoice::Builtin)
        e = p.elo > 0 ? p.elo : 1300.0 + 85.0 * p.skill;
    else
        e = 1100.0;
    e -= 900.0 * p.blunderChance;
    e -= 1.5 * p.randomness;
    if (p.candidates > 1) e -= std::max(0, p.window - 60) * 1.0;
    e = std::clamp(e, 400.0, 3200.0);
    return static_cast<int>(std::lround(e / 10.0) * 10);
}

std::string playerProfileToJson(const PlayerProfile& p) {
    std::string name;
    for (char c : p.name) {
        if (c == '"' || c == '\\') name += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) name += c;
    }
    return "{\n  \"name\": \"" + name + "\",\n  \"rating\": " + std::to_string(p.rating) +
           ",\n  \"peak\": " + std::to_string(p.peak) + ",\n  \"games\": " + std::to_string(p.games) +
           ",\n  \"wins\": " + std::to_string(p.wins) + ",\n  \"draws\": " + std::to_string(p.draws) +
           ",\n  \"losses\": " + std::to_string(p.losses) + ",\n  \"lastDelta\": " + std::to_string(p.lastDelta) +
           "\n}\n";
}

bool playerProfileFromJson(const std::string& text, PlayerProfile& p) {
    const auto v = json::parse(text);
    if (!v || !v->isObject()) return false;
    p.name = v->getString("name", p.name);
    auto num = [&](const char* k, int& field) { field = static_cast<int>(v->getNumber(k, field)); };
    num("rating", p.rating);
    num("peak", p.peak);
    num("games", p.games);
    num("wins", p.wins);
    num("draws", p.draws);
    num("losses", p.losses);
    num("lastDelta", p.lastDelta);
    return true;
}

}  // namespace chess
