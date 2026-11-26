#include "engine/engine.hpp"

namespace chess {

bool isStandardChessPosition(const Position& pos) {
    if (pos.width() != 8 || pos.height() != 8) return false;
    static const char* const kIds[] = {"pawn", "knight", "bishop", "rook", "queen", "king"};
    const auto& types = pos.registry().all();
    if (types.size() != 6) return false;
    for (size_t i = 0; i < types.size(); ++i)
        if (types[i].id != kIds[i] || types[i].customMoves) return false;
    return true;
}

bool usesStandardRules(const Game& game) {
    const Variant& v = game.variant();
    return v.standardEndRules && v.hooks.onSetup.empty() && v.hooks.filterMoves.empty() &&
           v.hooks.onMovePlayed.empty() && v.hooks.checkGameEnd.empty() && isStandardChessPosition(game.position());
}

}  // namespace chess
