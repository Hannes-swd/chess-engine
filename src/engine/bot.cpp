#include "engine/bot.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace chess {

StrengthModel strengthModel(int elo) {
    elo = std::clamp(elo, 400, 3200);
    StrengthModel m;
    // Wie weit schaut ein Spieler dieser Staerke voraus? Einsteiger sehen nur den naechsten Zug
    // (und direkte Schlagabtausche), Vereinsspieler zwei, drei Zuege, Meister deutlich mehr.
    m.depth = elo < 800    ? 1
              : elo < 1100 ? 2
              : elo < 1400 ? 3
              : elo < 1700 ? 4
              : elo < 2000 ? 6
              : elo < 2300 ? 8
              : elo < 2700 ? 12
                           : 18;
    m.candidates = elo < 2400 ? 8 : 4;
    // Je schwaecher, desto eher wird ein etwas schlechterer Zug fuer "genauso gut" gehalten.
    // 600: ~190 cp, 1000: ~100, 1400: ~55, 1800: ~30, 2200: ~16, 2600: ~9
    m.temperature = 260.0 * std::exp(-(elo - 400) / 650.0);
    // Echte Aussetzer. 600: ~13 %, 1000: ~6 %, 1400: ~3 %, 1800: ~1,4 %, 2200: ~0,7 %
    m.blunder = 0.18 * std::exp(-(elo - 400) / 550.0);
    return m;
}

BotProfile defaultBotProfile() {
    BotProfile p;
    p.id = "standard";
    p.name = "Standard";
    p.description = "Neutraler Bot mit voller Stärke";
    return p;
}

std::optional<BotProfile> parseBotProfile(const json::Value& v, std::string& error) {
    if (!v.isObject()) {
        error = "Profil muss ein JSON-Objekt sein";
        return std::nullopt;
    }
    BotProfile p;
    p.name = v.getString("name", "");
    if (p.name.empty()) {
        error = "Feld \"name\" fehlt";
        return std::nullopt;
    }
    p.description = v.getString("description", "");

    const std::string engine = v.getString("engine", "auto");
    if (engine == "auto")
        p.engine = EngineChoice::Auto;
    else if (engine == "stockfish")
        p.engine = EngineChoice::Stockfish;
    else if (engine == "builtin")
        p.engine = EngineChoice::Builtin;
    else {
        error = "\"engine\" muss \"auto\", \"stockfish\" oder \"builtin\" sein";
        return std::nullopt;
    }

    auto num = [&](const char* key, double fallback, double lo, double hi) {
        return std::clamp(v.getNumber(key, fallback), lo, hi);
    };
    p.strength = static_cast<int>(num("strength", p.strength, 0, 3200));
    if (p.strength > 0) p.strength = std::max(p.strength, 400);
    p.skill = static_cast<int>(num("skill", p.skill, 0, 20));
    p.elo = static_cast<int>(num("elo", p.elo, 0, 4000));
    p.depth = static_cast<int>(num("depth", p.depth, 0, 99));
    p.movetimeMs = static_cast<int>(num("movetimeMs", p.movetimeMs, 0, 600000));
    p.candidates = static_cast<int>(num("candidates", p.candidates, 1, 50));
    p.window = static_cast<int>(num("window", p.window, 0, 10000));
    p.aggression = num("aggression", p.aggression, -1, 1);
    p.randomness = num("randomness", p.randomness, 0, 2000);
    p.blunderChance = num("blunderChance", p.blunderChance, 0, 1);

    if (const json::Value* w = v.find("weights")) {
        p.weights.capture = w->getNumber("capture", p.weights.capture);
        p.weights.check = w->getNumber("check", p.weights.check);
        p.weights.advance = w->getNumber("advance", p.weights.advance);
    }

    if (const json::Value* t = v.find("tempo")) {
        const std::string mode = t->getString("mode", "instant");
        if (mode == "instant")
            p.tempo.mode = TempoMode::Instant;
        else if (mode == "fixed")
            p.tempo.mode = TempoMode::Fixed;
        else if (mode == "human")
            p.tempo.mode = TempoMode::Human;
        else {
            error = "\"tempo.mode\" muss \"instant\", \"fixed\" oder \"human\" sein";
            return std::nullopt;
        }
        p.tempo.minMs = static_cast<int>(std::clamp(t->getNumber("minMs", p.tempo.minMs), 0.0, 600000.0));
        p.tempo.maxMs = static_cast<int>(std::clamp(t->getNumber("maxMs", p.tempo.maxMs), 0.0, 600000.0));
        p.tempo.maxMs = std::max(p.tempo.maxMs, p.tempo.minMs);
        p.tempo.variance = std::clamp(t->getNumber("variance", p.tempo.variance), 0.0, 2.0);
    }
    p.pressureBlunder = num("pressureBlunder", p.pressureBlunder, 0, 1);
    return p;
}

const char* tempoModeName(TempoMode m) {
    switch (m) {
        case TempoMode::Instant: return "Sofort";
        case TempoMode::Fixed: return "Fest";
        case TempoMode::Human: return "Wie ein Mensch";
    }
    return "?";
}

namespace {

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += ch;
        } else if (ch == '\n') {
            out += "\\n";
        } else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += ch;
        }
    }
    return out + "\"";
}

std::string num(double v) {
    char buf[32];
    if (v == static_cast<double>(static_cast<long long>(v)))
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    else
        std::snprintf(buf, sizeof(buf), "%.3g", v);
    return buf;
}

}  // namespace

std::string botProfileToJson(const BotProfile& p) {
    const char* engine = p.engine == EngineChoice::Stockfish ? "stockfish"
                         : p.engine == EngineChoice::Builtin ? "builtin"
                                                             : "auto";
    const char* mode = p.tempo.mode == TempoMode::Human ? "human" : p.tempo.mode == TempoMode::Fixed ? "fixed" : "instant";
    std::string s = "{\n";
    s += "  \"name\": " + jsonString(p.name) + ",\n";
    s += "  \"description\": " + jsonString(p.description) + ",\n";
    s += std::string("  \"engine\": \"") + engine + "\",\n";
    s += "  \"strength\": " + num(p.strength) + ",\n";
    s += "  \"skill\": " + num(p.skill) + ",\n";
    s += "  \"elo\": " + num(p.elo) + ",\n";
    s += "  \"depth\": " + num(p.depth) + ",\n";
    s += "  \"movetimeMs\": " + num(p.movetimeMs) + ",\n";
    s += "  \"candidates\": " + num(p.candidates) + ",\n";
    s += "  \"window\": " + num(p.window) + ",\n";
    s += "  \"aggression\": " + num(p.aggression) + ",\n";
    s += "  \"randomness\": " + num(p.randomness) + ",\n";
    s += "  \"blunderChance\": " + num(p.blunderChance) + ",\n";
    s += "  \"pressureBlunder\": " + num(p.pressureBlunder) + ",\n";
    s += "  \"weights\": { \"capture\": " + num(p.weights.capture) + ", \"check\": " + num(p.weights.check) +
         ", \"advance\": " + num(p.weights.advance) + " },\n";
    s += std::string("  \"tempo\": { \"mode\": \"") + mode + "\", \"minMs\": " + num(p.tempo.minMs) +
         ", \"maxMs\": " + num(p.tempo.maxMs) + ", \"variance\": " + num(p.tempo.variance) + " }\n";
    s += "}\n";
    return s;
}

std::vector<BotProfile> loadBotProfiles(const std::filesystem::path& dir, std::vector<std::string>& errors) {
    std::vector<BotProfile> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return out;

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
    std::sort(files.begin(), files.end());

    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string err;
        const auto value = json::parse(ss.str(), &err);
        std::optional<BotProfile> p;
        if (value) p = parseBotProfile(*value, err);
        if (!p) {
            errors.push_back(file.filename().string() + ": " + err);
            continue;
        }
        p->id = file.stem().string();
        out.push_back(std::move(*p));
    }
    return out;
}

namespace {

bool givesCheck(const Position& pos, const Move& m) {
    Position c = pos;
    c.makeMove(m);
    return c.inCheck(c.sideToMove());
}

int forwardSteps(const Position& pos, const Move& m) {
    return (pos.rankOf(m.to) - pos.rankOf(m.from)) * pos.forward(pos.at(m.from).color);
}

}  // namespace

Bot::Bot(BotProfile profile, uint32_t seed) : profile_(std::move(profile)), rng_(seed) {}

int Bot::thinkDelayMs(const Game& game, const BotContext& ctx) {
    const Tempo& t = profile_.tempo;
    if (t.mode == TempoMode::Instant) return 0;

    const double lo = std::max(0, t.minMs);
    const double hi = std::max(lo, static_cast<double>(t.maxMs));
    double ms = lo;

    if (t.mode == TempoMode::Human) {
        const Position& pos = game.position();
        const std::vector<Move> legal = game.legalMoves();
        const int n = static_cast<int>(legal.size());
        if (n <= 1) {
            ms = lo * 0.4;  // nur ein Zug moeglich: kaum Nachdenken
        } else {
            // Mehr Moeglichkeiten -> laenger ueberlegen
            double f = 0.6 + 0.4 * std::min(2.0, n / 30.0);
            if (pos.inCheck(pos.sideToMove())) f *= 1.2;
            if (game.ply() < 8) f *= 0.45;  // Eroeffnung geht schnell ("kennt man")
            if (game.ply() > 0) {
                const Move& last = game.moves().back();
                if (last.flags & (FlagCapture | FlagEnPassant))
                    for (const Move& m : legal)
                        if (m.to == last.to && (m.flags & FlagCapture)) {
                            f *= 0.6;  // Zurueckschlagen ist meist schnell klar
                            break;
                        }
            }
            std::normal_distribution<double> noise(0.0, t.variance);
            const double mid = std::sqrt(std::max(1.0, lo) * std::max(1.0, hi));
            ms = std::clamp(mid * f * std::exp(noise(rng_)), lo * 0.4, hi);
        }
    }

    // Mit Uhr: nie mehr Zeit nehmen, als die Partie hergibt; in Zeitnot deutlich schneller
    if (ctx.remainingMs >= 0) {
        double cap = ctx.remainingMs / 25.0 + ctx.incrementMs * 0.8;
        if (ctx.remainingMs < 10000) cap = std::min(cap, ctx.remainingMs / 12.0);
        ms = std::min(ms, std::max(0.0, cap));
    }
    return static_cast<int>(ms);
}

Bot::Choice Bot::choose(const Game& game, Engine& engine, const BotContext& ctx) {
    Choice choice;
    const Position& pos = game.position();
    const std::vector<Move> legal = game.legalMoves();
    if (legal.empty()) return choice;
    const auto isLegal = [&](const Move& m) { return std::find(legal.begin(), legal.end(), m) != legal.end(); };

    const auto randomMove = [&](const Move* avoid) {
        std::vector<Move> pool;
        for (const Move& m : legal)
            if (!avoid || !(m == *avoid)) pool.push_back(m);
        if (pool.empty()) pool = legal;
        std::uniform_int_distribution<size_t> pick(0, pool.size() - 1);
        return pool[pick(rng_)];
    };

    const bool human = profile_.strength > 0;
    const StrengthModel model = strengthModel(profile_.strength);

    SearchLimits limits;
    // Menschliches Modell: nur so weit vorausschauen, wie ein Spieler dieser Staerke es tut
    limits.depth = human ? model.depth : profile_.depth;
    limits.movetimeMs = profile_.movetimeMs;
    if (ctx.remainingMs >= 0) {
        // Rechenzeit an die Uhr anpassen
        const int budget = std::max(30, ctx.remainingMs / 40 + ctx.incrementMs / 2);
        limits.movetimeMs = limits.movetimeMs > 0 ? std::min(limits.movetimeMs, budget) : budget;
    }
    limits.multiPv = std::max(1, human ? std::max(model.candidates, profile_.candidates) : profile_.candidates);
    if (!game.variant().hooks.filterMoves.empty()) limits.rootMoves = legal;  // Plugin-Regeln respektieren

    // Im menschlichen Modell rechnet die Engine voll (nur flach); die Schwaeche kommt aus Tiefe und Auswahl
    engine.configureStrength(human ? 20 : profile_.skill, human ? 0 : profile_.elo);
    const SearchResult res = engine.search(pos, limits);

    std::vector<ScoredMove> cands;
    for (const ScoredMove& l : res.lines)
        if (isLegal(l.move)) cands.push_back(l);

    if (cands.empty()) {
        choice.move = res.bestMove.valid() && isLegal(res.bestMove) ? res.bestMove : randomMove(nullptr);
        choice.reason = res.ok ? "Keine Bewertung – Engine-Zug" : "Engine-Fehler – Zufallszug";
        return choice;
    }

    if (human) return chooseHuman(pos, legal, cands, model, ctx);

    // Anker = der Zug, den die Engine selbst spielen wuerde. Bei begrenzter Staerke (Skill/Elo)
    // ist das nicht unbedingt die beste Linie; staerkere Kandidaten werden dann nicht gewaehlt.
    size_t anchorIdx = 0;
    for (size_t i = 0; i < cands.size(); ++i)
        if (cands[i].move == res.bestMove) {
            anchorIdx = i;
            break;
        }
    const ScoredMove& anchor = cands[anchorIdx];

    // Fehlerneigung: Grundwert plus Nervositaet, wenn die Uhr fast abgelaufen ist
    double blunder = profile_.blunderChance;
    bool pressure = false;
    if (ctx.remainingMs >= 0 && ctx.remainingMs < 10000 && profile_.pressureBlunder > 0) {
        blunder += profile_.pressureBlunder * (1.0 - ctx.remainingMs / 10000.0);
        pressure = true;
    }
    blunder = std::min(1.0, blunder);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    if (blunder > 0 && legal.size() > 1 && unit(rng_) < blunder) {
        choice.move = randomMove(&anchor.move);
        choice.reason = pressure ? "Fehler in Zeitnot" : "Absichtlicher Fehler (Lernmodus)";
        for (const auto& c : cands)
            if (c.move == choice.move) choice.score = c.score;
        return choice;
    }

    if (anchor.score.mate && anchor.score.winning) {
        choice.move = anchor.move;
        choice.score = anchor.score;
        choice.reason = "Matt in Sicht";
        return choice;
    }

    const int anchorCp = anchor.score.clamped(3000);
    std::uniform_real_distribution<double> noise(-profile_.randomness, profile_.randomness);
    const ScoredMove* pick = &anchor;
    double bestAdj = -1e18;
    for (const ScoredMove& c : cands) {
        const int cp = c.score.clamped(3000);
        if (anchorIdx != 0 && cp > anchorCp) continue;
        if (anchorCp - cp > profile_.window) continue;

        double bonus = forwardSteps(pos, c.move) * profile_.weights.advance;
        if (c.move.flags & (FlagCapture | FlagEnPassant)) bonus += profile_.weights.capture;
        if (givesCheck(pos, c.move)) bonus += profile_.weights.check;

        const double adj = cp + profile_.aggression * bonus + (profile_.randomness > 0 ? noise(rng_) : 0.0);
        if (adj > bestAdj) {
            bestAdj = adj;
            pick = &c;
        }
    }

    choice.move = pick->move;
    choice.score = pick->score;
    if (pick == &anchor)
        choice.reason = "Bester Kandidat";
    else if (profile_.aggression > 0.05)
        choice.reason = "Aggressive Wahl";
    else if (profile_.aggression < -0.05)
        choice.reason = "Vorsichtige Wahl";
    else
        choice.reason = "Zufällige Variation";
    return choice;
}

Bot::Choice Bot::chooseHuman(const Position& pos, const std::vector<Move>& legal, const std::vector<ScoredMove>& cands,
                             const StrengthModel& model, const BotContext& ctx) {
    Choice choice;
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    // Aussetzer: Grundwert aus der Elo, plus Profil-Patzer, plus Nervositaet in Zeitnot
    double blunder = model.blunder + profile_.blunderChance;
    bool pressure = false;
    if (ctx.remainingMs >= 0 && ctx.remainingMs < 10000 && profile_.pressureBlunder > 0) {
        blunder += profile_.pressureBlunder * (1.0 - ctx.remainingMs / 10000.0);
        pressure = true;
    }
    blunder = std::min(1.0, blunder);
    if (legal.size() > 1 && unit(rng_) < blunder) {
        // Menschliche Aussetzer sind meist plausible, aber schlechte Zuege – selten voellig wirre.
        // Darum: aus der schlechteren Haelfte der Kandidaten waehlen, nur manchmal irgendeinen Zug.
        std::vector<const ScoredMove*> worse;
        for (size_t i = cands.size() / 2; i < cands.size(); ++i) worse.push_back(&cands[i]);
        if (worse.empty() || cands.size() < 3 || unit(rng_) < 0.25) {
            std::vector<Move> pool;
            for (const Move& m : legal)
                if (!(m == cands.front().move)) pool.push_back(m);
            std::uniform_int_distribution<size_t> pick(0, pool.size() - 1);
            choice.move = pool[pick(rng_)];
        } else {
            std::uniform_int_distribution<size_t> pick(0, worse.size() - 1);
            choice.move = worse[pick(rng_)]->move;
        }
        for (const auto& c : cands)
            if (c.move == choice.move) choice.score = c.score;
        choice.reason = pressure ? "Fehler in Zeitnot" : "Übersehen";
        return choice;
    }

    // Gewichtete Wahl: Zuege nahe am besten sind wahrscheinlich, deutlich schlechtere selten.
    // Der Stil verschiebt die Bewertung (aggressiv mag Schlaege und Schachs), der Zufall streut.
    const double t = std::max(1.0, model.temperature + profile_.randomness);
    std::vector<double> adj;
    adj.reserve(cands.size());
    for (const ScoredMove& c : cands) {
        double bonus = forwardSteps(pos, c.move) * profile_.weights.advance;
        if (c.move.flags & (FlagCapture | FlagEnPassant)) bonus += profile_.weights.capture;
        if (givesCheck(pos, c.move)) bonus += profile_.weights.check;
        adj.push_back(c.score.clamped(3000) + profile_.aggression * bonus);
    }
    const double top = *std::max_element(adj.begin(), adj.end());
    std::vector<double> weights;
    weights.reserve(adj.size());
    for (double a : adj) weights.push_back(std::exp((a - top) / t));
    std::discrete_distribution<size_t> pick(weights.begin(), weights.end());
    const size_t idx = pick(rng_);

    choice.move = cands[idx].move;
    choice.score = cands[idx].score;
    const int loss = cands.front().score.clamped(3000) - cands[idx].score.clamped(3000);
    if (idx == 0)
        choice.reason = "Bester Kandidat";
    else if (loss >= 150)
        choice.reason = "Übersehen";
    else if (profile_.aggression > 0.05)
        choice.reason = "Aggressive Wahl";
    else if (profile_.aggression < -0.05)
        choice.reason = "Vorsichtige Wahl";
    else
        choice.reason = "Nicht ganz genau";
    return choice;
}

}  // namespace chess
