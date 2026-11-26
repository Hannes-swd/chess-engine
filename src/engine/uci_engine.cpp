#include "engine/uci_engine.hpp"

#include "core/fen.hpp"
#include "core/notation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace chess {

namespace {

std::vector<std::string> tokens(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> out;
    std::string t;
    while (in >> t) out.push_back(t);
    return out;
}

int toInt(const std::string& s, int fallback = 0) {
    try {
        return std::stoi(s);
    } catch (...) {
        return fallback;
    }
}

struct RawLine {
    int depth = 0;
    Score score;
    std::vector<std::string> pv;
};

}  // namespace

std::unique_ptr<UciEngine> UciEngine::launch(const std::filesystem::path& executable, std::string& error) {
    std::unique_ptr<UciEngine> e(new UciEngine());
    if (!e->proc_.start(executable, error)) return nullptr;
    if (!e->handshake(error)) return nullptr;
    return e;
}

UciEngine::~UciEngine() {
    proc_.writeLine("quit");
    proc_.stop();
}

bool UciEngine::handshake(std::string& error) {
    proc_.writeLine("uci");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        auto line = proc_.readLine(std::chrono::milliseconds(200));
        if (!line) {
            if (proc_.finished()) break;
            continue;
        }
        if (line->rfind("id name ", 0) == 0) {
            name_ = line->substr(8);
        } else if (line->rfind("option name ", 0) == 0) {
            const size_t typePos = line->find(" type ");
            if (typePos == std::string::npos) continue;
            const std::string optName = line->substr(12, typePos - 12);
            Option opt;
            const auto t = tokens(line->substr(typePos));
            for (size_t i = 0; i + 1 < t.size(); ++i) {
                if (t[i] == "type") opt.type = t[i + 1];
                if (t[i] == "min") opt.min = toInt(t[i + 1]);
                if (t[i] == "max") opt.max = toInt(t[i + 1]);
            }
            options_[optName] = opt;
        } else if (*line == "uciok") {
            if (!sync()) {
                error = "Engine antwortet nicht auf isready";
                return false;
            }
            return true;
        }
    }
    error = "Keine gültige UCI-Engine (kein 'uciok' erhalten)";
    return false;
}

bool UciEngine::waitFor(const std::string& token, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto line = proc_.readLine(std::chrono::milliseconds(100));
        if (line && *line == token) return true;
        if (!line && proc_.finished()) return false;
    }
    return false;
}

bool UciEngine::sync() {
    proc_.writeLine("isready");
    return waitFor("readyok", std::chrono::seconds(10));
}

int UciEngine::clampOption(const std::string& name, int value) const {
    const auto it = options_.find(name);
    if (it == options_.end() || it->second.type != "spin" || it->second.max <= it->second.min) return value;
    return std::clamp(value, it->second.min, it->second.max);
}

void UciEngine::setOption(const std::string& name, const std::string& value) {
    proc_.writeLine("setoption name " + name + " value " + value);
}

void UciEngine::configureStrength(int skill, int elo) {
    if (skill == skill_ && elo == elo_) return;
    skill_ = skill;
    elo_ = elo;
    if (hasOption("UCI_LimitStrength")) setOption("UCI_LimitStrength", elo > 0 ? "true" : "false");
    if (elo > 0 && hasOption("UCI_Elo")) setOption("UCI_Elo", std::to_string(clampOption("UCI_Elo", elo)));
    if (hasOption("Skill Level")) setOption("Skill Level", std::to_string(clampOption("Skill Level", skill)));
    sync();
}

void UciEngine::newGame() {
    proc_.writeLine("ucinewgame");
    sync();
}

SearchResult UciEngine::search(const Position& pos, const SearchLimits& limits) {
    SearchResult res;
    if (!supportsPosition(pos)) {
        res.error = name_ + " unterstützt nur Standardschach auf 8x8";
        return res;
    }

    const int wantPv = std::max(1, limits.multiPv);
    if (wantPv != multiPv_ && hasOption("MultiPV")) {
        setOption("MultiPV", std::to_string(clampOption("MultiPV", wantPv)));
        multiPv_ = wantPv;
    }

    proc_.writeLine("position fen " + toFen(pos));

    std::string go = "go";
    if (limits.depth > 0) go += " depth " + std::to_string(limits.depth);
    if (limits.movetimeMs > 0) go += " movetime " + std::to_string(limits.movetimeMs);
    if (limits.nodes > 0) go += " nodes " + std::to_string(limits.nodes);
    if (limits.depth <= 0 && limits.movetimeMs <= 0 && limits.nodes == 0) go += " depth 12";
    if (!limits.rootMoves.empty()) {
        go += " searchmoves";
        for (const Move& m : limits.rootMoves) go += " " + toUci(pos, m);
    }
    proc_.writeLine(go);

    std::map<int, RawLine> raw;
    std::string best;
    bool stopSent = false;
    while (true) {
        if (limits.stop && limits.stop->load() && !stopSent) {
            proc_.writeLine("stop");
            stopSent = true;
        }
        auto line = proc_.readLine(std::chrono::milliseconds(100));
        if (!line) {
            if (proc_.finished()) {
                res.error = name_ + " wurde unerwartet beendet";
                return res;
            }
            continue;
        }
        const auto t = tokens(*line);
        if (t.empty()) continue;
        if (t[0] == "bestmove") {
            best = t.size() > 1 ? t[1] : "";
            break;
        }
        if (t[0] != "info") continue;

        RawLine rl;
        int multipv = 1;
        bool hasScore = false, bound = false;
        for (size_t i = 1; i < t.size(); ++i) {
            if (t[i] == "depth" && i + 1 < t.size()) {
                rl.depth = toInt(t[++i]);
            } else if (t[i] == "multipv" && i + 1 < t.size()) {
                multipv = toInt(t[++i], 1);
            } else if (t[i] == "score" && i + 2 < t.size()) {
                const int v = toInt(t[i + 2]);
                if (t[i + 1] == "cp") {
                    rl.score = Score::centipawns(v);
                    hasScore = true;
                } else if (t[i + 1] == "mate") {
                    rl.score = Score::mateIn(std::abs(v), v > 0);
                    hasScore = true;
                }
                i += 2;
            } else if (t[i] == "lowerbound" || t[i] == "upperbound") {
                bound = true;
            } else if (t[i] == "pv") {
                rl.pv.assign(t.begin() + static_cast<std::ptrdiff_t>(i) + 1, t.end());
                break;
            }
        }
        if (hasScore && !bound && !rl.pv.empty()) raw[multipv] = std::move(rl);
    }

    // UCI-Texte erst am Ende in Zuege uebersetzen (spart Arbeit bei vielen info-Zeilen)
    for (auto& [idx, rl] : raw) {
        ScoredMove sm;
        sm.score = rl.score;
        sm.depth = rl.depth;
        Position walk = pos;
        for (const std::string& uci : rl.pv) {
            const auto m = parseUciMove(walk, uci);
            if (!m) break;
            sm.pv.push_back(*m);
            walk.makeMove(*m);
        }
        if (sm.pv.empty()) continue;
        sm.move = sm.pv.front();
        res.lines.push_back(std::move(sm));
    }

    Position tmp = pos;
    if (const auto m = parseUciMove(tmp, best)) res.bestMove = *m;
    if (!res.bestMove.valid() && !res.lines.empty()) res.bestMove = res.lines.front().move;
    res.ok = true;
    return res;
}

}  // namespace chess
