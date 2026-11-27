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
    if (p.strength > 0) {
        // Menschliches Modell: Ziel-Elo, nur zusaetzliche Patzer/Zufall aus dem Profil ziehen ab.
        // Ohne Stockfish ist die Bewertung der eingebauten Engine grober -> etwas schwaecher.
        e = p.strength;
        if (!withStockfish && p.strength > 1400) e = 1400 + (p.strength - 1400) * 0.4;
        e -= 900.0 * p.blunderChance + 1.5 * p.randomness;
        e = std::clamp(e, 400.0, 3200.0);
        return static_cast<int>(std::lround(e / 10.0) * 10);
    }
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

namespace {

std::string escaped(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) out += c;
    }
    return out;
}

std::string historyJson(const PlayerProfile& p) {
    std::string out;
    for (size_t i = 0; i < p.history.size(); ++i) {
        const auto& h = p.history[i];
        out += std::string(i ? ",\n    " : "\n    ") + "{\"date\": \"" + escaped(h.date) + "\", \"rating\": " +
               std::to_string(h.rating) + ", \"opponent\": \"" + escaped(h.opponent) + "\"}";
    }
    return out + (p.history.empty() ? "" : "\n  ");
}

}  // namespace

int applyResult(PlayerProfile& p, int opponentElo, double score, const std::string& date, const std::string& opponent) {
    if (p.history.empty()) p.history.push_back({"", p.rating, ""});  // Startpunkt der Kurve
    const int d = applyResult(p, opponentElo, score);
    p.history.push_back({date, p.rating, opponent});
    if (p.history.size() > 2000) p.history.erase(p.history.begin() + 1);
    return d;
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
           ",\n  \"history\": [" + historyJson(p) + "]\n}\n";
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
    if (const json::Value* h = v->find("history"); h && h->isArray()) {
        p.history.clear();
        for (const json::Value& e : h->asArray())
            if (e.isObject())
                p.history.push_back({e.getString("date", ""), static_cast<int>(e.getNumber("rating", p.rating)),
                                     e.getString("opponent", "")});
    }
    return true;
}

}  // namespace chess
