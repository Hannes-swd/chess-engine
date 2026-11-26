#include "core/tactics.hpp"

#include <algorithm>
#include <climits>
#include <vector>

namespace chess {

int materialOf(const Position& pos, Color c) {
    int sum = 0;
    for (Square s = 0; s < pos.squareCount(); ++s) {
        const Piece& p = pos.at(s);
        if (p.empty() || p.color != c) continue;
        const PieceType& t = pos.registry().get(p.type);
        if (!t.royal) sum += t.value;
    }
    return sum;
}

int materialBalance(const Position& pos, Color c) { return materialOf(pos, c) - materialOf(pos, opposite(c)); }

bool isEnPrise(const Position& pos, Square sq) {
    const Piece& p = pos.at(sq);
    if (p.empty()) return false;
    const PieceRegistry& reg = pos.registry();
    Position probe = pos;
    probe.setSideToMove(opposite(p.color));
    probe.setEnPassant(NoSquare);
    std::vector<Move> moves;
    probe.generatePseudoLegal(moves);
    int cheapest = INT_MAX;
    for (const Move& m : moves) {
        if (m.to != sq || !(m.flags & FlagCapture)) continue;
        const PieceType& t = reg.get(probe.at(m.from).type);
        cheapest = std::min(cheapest, t.royal ? 1'000'000 : t.value);
    }
    if (cheapest == INT_MAX) return false;
    const bool defended = pos.isAttacked(sq, p.color);
    return !defended || cheapest < reg.get(p.type).value;
}

std::string pieceAccusative(const PieceType& t) {
    if (t.id == "pawn") return "einen Bauern";
    if (t.id == "knight") return "einen Springer";
    if (t.id == "bishop") return "einen Läufer";
    if (t.id == "rook") return "einen Turm";
    if (t.id == "queen") return "die Dame";
    if (t.id == "king") return "den König";
    return "die Figur „" + t.name + "“";
}

std::string pieceNominative(const PieceType& t) {
    if (t.id == "pawn") return "Der Bauer";
    if (t.id == "knight") return "Der Springer";
    if (t.id == "bishop") return "Der Läufer";
    if (t.id == "rook") return "Der Turm";
    if (t.id == "queen") return "Die Dame";
    if (t.id == "king") return "Der König";
    return "Die Figur „" + t.name + "“";
}

}  // namespace chess
