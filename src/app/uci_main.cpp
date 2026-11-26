// Die eingebaute Engine als eigenstaendiges UCI-Programm.
// Damit kann sie in jeder Schach-GUI benutzt werden, und der UCI-Client laesst sich ohne Stockfish testen.

#include "core/fen.hpp"
#include "core/game.hpp"
#include "core/notation.hpp"
#include "engine/builtin_engine.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace chess;

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

std::string uciScore(const Score& s) {
    if (s.mate) return "mate " + std::to_string(s.winning ? s.value : -s.value);
    return "cp " + std::to_string(s.value);
}

bool isGoKeyword(const std::string& t) {
    static const char* const kw[] = {"depth", "movetime", "nodes",     "wtime",    "btime", "winc",
                                     "binc",  "movestogo", "infinite", "ponder", "mate"};
    for (const char* k : kw)
        if (t == k) return true;
    return false;
}

}  // namespace

int main() {
    BuiltinEngine engine;
    const auto registry = PieceRegistry::standard();
    Position pos = fromFen(registry, kStandardStartFen);
    int multiPv = 1;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto t = tokens(line);
        if (t.empty()) continue;
        const std::string& cmd = t[0];

        if (cmd == "uci") {
            std::cout << "id name ChessEngine Builtin\n"
                         "id author ChessEngine-Projekt\n"
                         "option name MultiPV type spin default 1 min 1 max 64\n"
                         "uciok"
                      << std::endl;
        } else if (cmd == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (cmd == "setoption") {
            // setoption name MultiPV value N
            if (t.size() >= 5 && t[2] == "MultiPV" && t[3] == "value") multiPv = std::max(1, toInt(t[4], 1));
        } else if (cmd == "position") {
            try {
                size_t i = 1;
                if (i < t.size() && t[i] == "startpos") {
                    pos = fromFen(registry, kStandardStartFen);
                    ++i;
                } else if (i < t.size() && t[i] == "fen") {
                    std::string fen;
                    for (++i; i < t.size() && t[i] != "moves"; ++i) fen += (fen.empty() ? "" : " ") + t[i];
                    pos = fromFen(registry, fen);
                }
                if (i < t.size() && t[i] == "moves")
                    for (++i; i < t.size(); ++i) {
                        const auto m = parseUciMove(pos, t[i]);
                        if (!m) {
                            std::cout << "info string Ungültiger Zug: " << t[i] << std::endl;
                            break;
                        }
                        pos.makeMove(*m);
                    }
            } catch (const std::exception& e) {
                std::cout << "info string Fehler: " << e.what() << std::endl;
            }
        } else if (cmd == "go") {
            SearchLimits lim;
            lim.multiPv = multiPv;
            int myTime = 0, myInc = 0, movesToGo = 30;
            const bool white = pos.sideToMove() == Color::White;
            for (size_t i = 1; i < t.size(); ++i) {
                const bool hasArg = i + 1 < t.size();
                if (t[i] == "depth" && hasArg) lim.depth = toInt(t[++i]);
                else if (t[i] == "movetime" && hasArg) lim.movetimeMs = toInt(t[++i]);
                else if (t[i] == "nodes" && hasArg) lim.nodes = static_cast<uint64_t>(toInt(t[++i]));
                else if (t[i] == (white ? "wtime" : "btime") && hasArg) myTime = toInt(t[++i]);
                else if (t[i] == (white ? "winc" : "binc") && hasArg) myInc = toInt(t[++i]);
                else if (t[i] == "movestogo" && hasArg) movesToGo = std::max(1, toInt(t[++i]));
                else if (t[i] == "searchmoves") {
                    while (i + 1 < t.size() && !isGoKeyword(t[i + 1])) {
                        if (const auto m = parseUciMove(pos, t[++i])) lim.rootMoves.push_back(*m);
                    }
                }
            }
            if (myTime > 0 && lim.movetimeMs == 0) lim.movetimeMs = std::max(10, myTime / movesToGo + myInc / 2);
            if (lim.depth == 0 && lim.movetimeMs == 0 && lim.nodes == 0) lim.depth = 5;

            const SearchResult res = engine.search(pos, lim);
            for (size_t i = 0; i < res.lines.size(); ++i) {
                const auto& l = res.lines[i];
                std::cout << "info depth " << l.depth << " multipv " << i + 1 << " score " << uciScore(l.score)
                          << " nodes " << res.nodes << " pv";
                for (const Move& m : l.pv) std::cout << ' ' << toUci(pos, m);
                std::cout << '\n';
            }
            std::cout << "bestmove " << (res.bestMove.valid() ? toUci(pos, res.bestMove) : "0000") << std::endl;
        } else if (cmd == "quit") {
            break;
        }
        // "ucinewgame", "stop", "ponderhit": nichts zu tun (Suche laeuft synchron)
    }
    return 0;
}
