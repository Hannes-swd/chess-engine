#include "engine/training.hpp"

#include "core/fen.hpp"
#include "core/notation.hpp"
#include "util/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace chess {

namespace {

std::string escaped(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) out += c;
    }
    return out;
}

// Wert der Figur auf `s` in Centipawns (0, wenn leer)
int valueAt(const Position& pos, Square s) {
    const Piece& p = pos.at(s);
    return p.empty() ? 0 : pos.registry().get(p.type).value;
}

}  // namespace

std::string mistakeTheme(const Game& game, const MoveReview& r) {
    const Position& after = game.positionAt(std::min(r.ply + 1, game.ply()));
    if (r.playedScore.mate && !r.playedScore.winning) return "Matt zugelassen";
    if (r.bestScore.mate && r.bestScore.winning) return "Matt übersehen";
    // Die beste Antwort des Gegners schlaegt etwas: eingestellt
    if (!r.threatLine.empty() && (r.threatLine.front().flags & FlagCapture)) {
        const int v = valueAt(after, r.threatLine.front().to);
        if (v >= 300) return "Figur eingestellt";
        if (v > 0) return "Bauer eingestellt";
    }
    if (r.best.valid() && (r.best.flags & FlagCapture)) return "Schlagzug übersehen";
    if (r.quality == MoveQuality::Miss) return "Chance verpasst";
    return "Ungenauer Plan";
}

std::vector<Puzzle> puzzlesFromAnalysis(const Game& game, const GameAnalysis& a, std::optional<Color> side,
                                        const std::string& source, long long now) {
    std::vector<Puzzle> out;
    for (const MoveReview& r : a.moves) {
        if (side && r.mover != *side) continue;
        if (r.quality != MoveQuality::Mistake && r.quality != MoveQuality::Blunder && r.quality != MoveQuality::Miss)
            continue;
        if (!r.best.valid() || r.ply >= game.ply()) continue;
        const Position& before = game.positionAt(r.ply);
        Puzzle p;
        p.fen = toFen(before);
        p.bestUci = toUci(before, r.best);
        p.bestSan = r.bestSan;
        p.playedSan = r.san;
        p.theme = mistakeTheme(game, r);
        p.source = source;
        p.quality = r.quality;
        p.due = now;
        out.push_back(std::move(p));
    }
    return out;
}

int mergePuzzles(TrainingData& data, const std::vector<Puzzle>& fresh) {
    int added = 0;
    for (const Puzzle& p : fresh) {
        const std::string k = p.key();
        if (std::any_of(data.puzzles.begin(), data.puzzles.end(), [&](const Puzzle& q) { return q.key() == k; }))
            continue;
        data.puzzles.push_back(p);
        ++added;
    }
    return added;
}

void schedule(Puzzle& p, bool correct, long long now) {
    static const long long kDays[] = {0, 1, 3, 7, 21, 60};
    if (correct) {
        ++p.solved;
        p.box = std::min(p.box + 1, 5);
        p.due = now + kDays[p.box] * 24 * 3600;
    } else {
        ++p.failed;
        p.box = 0;
        p.due = now + 10 * 60;
    }
}

std::vector<size_t> duePuzzles(const TrainingData& data, long long now) {
    std::vector<size_t> idx;
    for (size_t i = 0; i < data.puzzles.size(); ++i)
        if (data.puzzles[i].due <= now) idx.push_back(i);
    std::stable_sort(idx.begin(), idx.end(),
                     [&](size_t a, size_t b) { return data.puzzles[a].due < data.puzzles[b].due; });
    return idx;
}

std::string trainingToJson(const TrainingData& d) {
    std::string out = "{\n  \"puzzles\": [";
    for (size_t i = 0; i < d.puzzles.size(); ++i) {
        const Puzzle& p = d.puzzles[i];
        out += std::string(i ? "," : "") + "\n    {\"fen\": \"" + escaped(p.fen) + "\", \"best\": \"" +
               escaped(p.bestUci) + "\", \"bestSan\": \"" + escaped(p.bestSan) + "\", \"played\": \"" +
               escaped(p.playedSan) + "\", \"theme\": \"" + escaped(p.theme) + "\", \"source\": \"" +
               escaped(p.source) + "\", \"quality\": " + std::to_string(static_cast<int>(p.quality)) +
               ", \"box\": " + std::to_string(p.box) + ", \"due\": " + std::to_string(p.due) +
               ", \"solved\": " + std::to_string(p.solved) + ", \"failed\": " + std::to_string(p.failed) + "}";
    }
    out += d.puzzles.empty() ? "],\n" : "\n  ],\n";
    auto mapJson = [&](const char* key, const std::map<std::string, int>& m) {
        out += std::string("  \"") + key + "\": {";
        bool first = true;
        for (const auto& [k, v] : m) {
            out += std::string(first ? "" : ", ") + "\"" + escaped(k) + "\": " + std::to_string(v);
            first = false;
        }
        out += "}";
    };
    mapJson("openingRuns", d.openingRuns);
    out += ",\n";
    mapJson("openingClean", d.openingClean);
    out += ",\n  \"harvested\": [";
    for (size_t i = 0; i < d.harvested.size(); ++i) out += std::string(i ? ", " : "") + "\"" + escaped(d.harvested[i]) + "\"";
    return out + "]\n}\n";
}

bool trainingFromJson(const std::string& text, TrainingData& d) {
    const auto v = json::parse(text);
    if (!v || !v->isObject()) return false;
    d = {};
    if (const json::Value* list = v->find("puzzles"); list && list->isArray())
        for (const json::Value& e : list->asArray()) {
            if (!e.isObject()) continue;
            Puzzle p;
            p.fen = e.getString("fen", "");
            p.bestUci = e.getString("best", "");
            p.bestSan = e.getString("bestSan", "");
            p.playedSan = e.getString("played", "");
            p.theme = e.getString("theme", "");
            p.source = e.getString("source", "");
            p.quality = static_cast<MoveQuality>(std::clamp(static_cast<int>(e.getNumber("quality", 6)), 0, 9));
            p.box = static_cast<int>(e.getNumber("box", 0));
            p.due = static_cast<long long>(e.getNumber("due", 0));
            p.solved = static_cast<int>(e.getNumber("solved", 0));
            p.failed = static_cast<int>(e.getNumber("failed", 0));
            if (!p.fen.empty() && !p.bestUci.empty()) d.puzzles.push_back(std::move(p));
        }
    auto readMap = [&](const char* key, std::map<std::string, int>& m) {
        if (const json::Value* o = v->find(key); o && o->isObject())
            for (const auto& [k, val] : o->asObject()) m[k] = static_cast<int>(val.asNumber(0));
    };
    readMap("openingRuns", d.openingRuns);
    readMap("openingClean", d.openingClean);
    if (const json::Value* h = v->find("harvested"); h && h->isArray())
        for (const json::Value& e : h->asArray())
            if (e.isString()) d.harvested.push_back(e.asString());
    return true;
}

// --- Statistik ---------------------------------------------------------------------

namespace {

// "[%eval 0.35]" / "[%eval #-2]" -> Bewertung aus Sicht von Weiss
std::optional<Score> evalFromComment(const std::string& c) {
    const size_t at = c.find("[%eval ");
    if (at == std::string::npos) return std::nullopt;
    const size_t start = at + 7;
    const size_t end = c.find(']', start);
    if (end == std::string::npos) return std::nullopt;
    const std::string v = c.substr(start, end - start);
    if (v.empty()) return std::nullopt;
    if (v[0] == '#') {
        const int n = std::atoi(v.c_str() + 1);
        return Score::mateIn(std::abs(n), n > 0 || (n == 0 && v.find('-') == std::string::npos));
    }
    return Score::centipawns(static_cast<int>(std::lround(std::atof(v.c_str()) * 100.0)));
}

}  // namespace

std::optional<double> accuracyFromComments(const PgnRecord& rec, Color side) {
    Color mover = Color::White;
    if (const std::string fen = rec.tag("FEN"); fen.find(" b ") != std::string::npos) mover = Color::Black;
    Score prev = Score::centipawns(20);  // Startstellung: Weiss leicht vorn
    double sum = 0;
    int n = 0;
    for (size_t i = 0; i < rec.moves.size(); ++i, mover = opposite(mover)) {
        const std::string& c = i < rec.comments.size() ? rec.comments[i] : std::string();
        std::optional<Score> now = evalFromComment(c);
        if (!now && c.find("Matt") != std::string::npos)  // Matt gesetzt: aus Sicht des Ziehenden gewonnen
            now = mover == Color::White ? Score::mateIn(0, true) : Score::mateIn(0, false);
        if (!now) {
            continue;
        }
        if (mover == side) {
            const double before = winChance(side == Color::White ? prev : prev.negated());
            const double after = winChance(side == Color::White ? *now : now->negated());
            sum += moveAccuracy(before, after);
            ++n;
        }
        prev = *now;
    }
    if (n == 0) return std::nullopt;
    return sum / n;
}

std::array<int, kQualityCount> qualityCountsFromComments(const PgnRecord& rec, Color side) {
    std::array<int, kQualityCount> counts{};
    Color mover = Color::White;
    if (const std::string fen = rec.tag("FEN"); fen.find(" b ") != std::string::npos) mover = Color::Black;
    for (size_t i = 0; i < rec.moves.size(); ++i, mover = opposite(mover)) {
        if (mover != side || i >= rec.comments.size()) continue;
        std::string c = rec.comments[i];
        if (const size_t close = c.find("] "); c.rfind("[%eval", 0) == 0 && close != std::string::npos)
            c = c.substr(close + 2);
        int found = -1;
        size_t foundLen = 0;
        for (int q = 0; q < kQualityCount; ++q) {
            const std::string name = qualityName(static_cast<MoveQuality>(q));
            if (c.rfind(name, 0) == 0 && name.size() > foundLen) {
                found = q;
                foundLen = name.size();
            }
        }
        if (found >= 0) ++counts[static_cast<size_t>(found)];
    }
    return counts;
}

}  // namespace chess
