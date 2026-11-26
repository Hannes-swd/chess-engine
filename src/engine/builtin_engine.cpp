#include "engine/builtin_engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <utility>

namespace chess {

namespace {

constexpr int kInf = 1'000'000'000;
constexpr int kMate = 1'000'000;
constexpr int kMateBound = kMate - 10'000;
constexpr int kMaxPly = 64;

Score toScore(int s) {
    if (s > kMateBound) return Score::mateIn((kMate - s + 1) / 2, true);
    if (s < -kMateBound) return Score::mateIn((kMate + s) / 2, false);
    return Score::centipawns(s);
}

}  // namespace

int BuiltinEngine::evaluate(const Position& pos) const {
    const int w = pos.width();
    const int h = pos.height();
    const int span = (w - 1) + (h - 1);
    int score = 0;
    for (Square s = 0; s < pos.squareCount(); ++s) {
        const Piece& p = pos.at(s);
        if (p.empty()) continue;
        const PieceType& t = pos.registry().get(p.type);
        const int f = pos.fileOf(s);
        const int r = pos.rankOf(s);
        const int df = std::abs(2 * f - (w - 1));  // doppelter Abstand zur Mitte
        const int dr = std::abs(2 * r - (h - 1));

        int v = t.value;
        if (t.pawnLike) {
            const int advanced = p.color == Color::White ? r - pos.pawnStartRank(Color::White)
                                                         : pos.pawnStartRank(Color::Black) - r;
            v += advanced * advanced * 2 + advanced * 4 + (w - 1 - df);
        } else if (!t.royal) {
            v += (span - (df + dr)) * 2;  // Figuren gehoeren ins Zentrum
        }
        score += p.color == pos.sideToMove() ? v : -v;
    }
    return score;
}

void BuiltinEngine::orderMoves(const Position& pos, std::vector<Move>& moves) const {
    const PieceRegistry& reg = pos.registry();
    std::vector<std::pair<int, Move>> keyed;
    keyed.reserve(moves.size());
    for (const Move& m : moves) {
        int key = 0;
        if (m.flags & FlagCapture) {
            const Square victimSq = (m.flags & FlagEnPassant) ? pos.square(pos.fileOf(m.to), pos.rankOf(m.from)) : m.to;
            const Piece& victim = pos.at(victimSq);
            const int vv = victim.empty() ? 100 : reg.get(victim.type).value;
            const int av = reg.get(pos.at(m.from).type).value;
            key += 100'000 + 10 * vv - av / 10;  // MVV-LVA
        }
        if (m.promotion != NoPieceType) key += 50'000 + reg.get(m.promotion).value;
        keyed.emplace_back(key, m);
    }
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < moves.size(); ++i) moves[i] = keyed[i].second;
}

bool BuiltinEngine::checkAbort() {
    ++nodes_;
    if (!canAbort_) return false;
    if (aborted_) return true;
    if (nodeLimit_ && nodes_ >= nodeLimit_) aborted_ = true;
    if ((nodes_ & 1023) == 0) {
        if (stop_ && stop_->load()) aborted_ = true;
        if (hasDeadline_ && std::chrono::steady_clock::now() >= deadline_) aborted_ = true;
    }
    return aborted_;
}

int BuiltinEngine::quiesce(Position& pos, int alpha, int beta, int ply) {
    if (checkAbort()) return 0;
    const int stand = evaluate(pos);
    if (ply >= kMaxPly || stand >= beta) return stand;
    if (stand > alpha) alpha = stand;

    std::vector<Move> moves;
    pos.generatePseudoLegal(moves);
    std::erase_if(moves, [](const Move& m) { return !(m.flags & FlagCapture) && m.promotion == NoPieceType; });
    orderMoves(pos, moves);

    const Color us = pos.sideToMove();
    for (const Move& m : moves) {
        const auto u = pos.makeMove(m);
        if (pos.inCheck(us)) {
            pos.unmakeMove(m, u);
            continue;
        }
        const int s = -quiesce(pos, -beta, -alpha, ply + 1);
        pos.unmakeMove(m, u);
        if (aborted_) return 0;
        if (s >= beta) return s;
        if (s > alpha) alpha = s;
    }
    return alpha;
}

int BuiltinEngine::negamax(Position& pos, int depth, int alpha, int beta, int ply) {
    if (checkAbort()) return 0;
    const Color us = pos.sideToMove();
    const bool check = pos.inCheck(us);
    if (check && ply < kMaxPly / 2) ++depth;  // Schach-Verlaengerung: findet Matts zuverlaessiger
    if (depth <= 0) return quiesce(pos, alpha, beta, ply);
    if (ply >= kMaxPly) return evaluate(pos);

    std::vector<Move> moves;
    pos.generateLegal(moves);
    if (moves.empty()) return check ? -kMate + ply : 0;
    if (pos.halfmoveClock() >= 100) return 0;
    orderMoves(pos, moves);

    int best = -kInf;
    for (const Move& m : moves) {
        const auto u = pos.makeMove(m);
        const int s = -negamax(pos, depth - 1, -beta, -alpha, ply + 1);
        pos.unmakeMove(m, u);
        if (aborted_) return 0;
        if (s > best) best = s;
        if (s > alpha) alpha = s;
        if (alpha >= beta) break;
    }
    return best;
}

SearchResult BuiltinEngine::search(const Position& root, const SearchLimits& limits) {
    SearchResult res;
    Position pos = root;

    nodes_ = 0;
    nodeLimit_ = limits.nodes;
    stop_ = limits.stop;
    aborted_ = false;
    canAbort_ = false;  // Tiefe 1 wird immer fertig gerechnet
    const auto start = std::chrono::steady_clock::now();
    hasDeadline_ = limits.movetimeMs > 0;
    deadline_ = start + std::chrono::milliseconds(limits.movetimeMs);

    const int maxDepth = limits.depth > 0 ? limits.depth : (hasDeadline_ || nodeLimit_ ? kMaxPly : 4);
    const int multi = std::max(1, limits.multiPv);

    std::vector<Move> rootMoves;
    pos.generateLegal(rootMoves);
    if (!limits.rootMoves.empty())
        std::erase_if(rootMoves, [&](const Move& m) {
            return std::find(limits.rootMoves.begin(), limits.rootMoves.end(), m) == limits.rootMoves.end();
        });
    res.ok = true;
    if (rootMoves.empty()) return res;
    orderMoves(pos, rootMoves);

    std::vector<std::pair<Move, int>> completed;
    int completedDepth = 0;

    for (int d = 1; d <= maxDepth; ++d) {
        std::vector<std::pair<Move, int>> scored;
        for (const Move& m : rootMoves) {
            // MultiPV: Zuege ausserhalb der besten `multi` brauchen nur eine Schranke.
            int alpha = -kInf;
            if (static_cast<int>(scored.size()) >= multi) {
                std::vector<int> vals;
                vals.reserve(scored.size());
                for (const auto& p : scored) vals.push_back(p.second);
                std::nth_element(vals.begin(), vals.begin() + (multi - 1), vals.end(), std::greater<int>());
                alpha = vals[static_cast<size_t>(multi - 1)];
            }
            const auto u = pos.makeMove(m);
            const int s = -negamax(pos, d - 1, -kInf, -alpha, 1);
            pos.unmakeMove(m, u);
            if (aborted_) break;
            scored.emplace_back(m, s);
        }
        if (aborted_ && !completed.empty()) break;  // letzte vollstaendige Tiefe verwenden

        std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        completed = scored;
        completedDepth = d;
        canAbort_ = true;

        // Naechste Iteration startet mit der besten Reihenfolge
        std::vector<Move> reordered;
        for (const auto& p : scored) reordered.push_back(p.first);
        for (const Move& m : rootMoves)
            if (std::find(reordered.begin(), reordered.end(), m) == reordered.end()) reordered.push_back(m);
        rootMoves = std::move(reordered);

        if (aborted_) break;
        if (completed.front().second > kMateBound) break;  // Matt gefunden
        if (hasDeadline_ && std::chrono::steady_clock::now() - start > (deadline_ - start) / 2) break;
    }

    res.bestMove = completed.front().first;
    for (size_t i = 0; i < completed.size() && static_cast<int>(i) < multi; ++i) {
        ScoredMove sm;
        sm.move = completed[i].first;
        sm.score = toScore(completed[i].second);
        sm.depth = completedDepth;
        sm.pv = {sm.move};
        res.lines.push_back(std::move(sm));
    }
    res.nodes = nodes_;
    return res;
}

}  // namespace chess
