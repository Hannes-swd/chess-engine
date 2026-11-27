#pragma once

#include "engine/engine.hpp"

#include <chrono>

namespace chess {

// Einfache Alpha-Beta-Engine, die direkt auf dem generischen Kern arbeitet.
// Sie ist viel schwaecher als Stockfish, versteht dafuer aber JEDE Variante:
// andere Brettgroessen, Plugin-Figuren, eigene Zugfunktionen.
// Bewertung: Materialwerte aus der PieceRegistry + Zentrum + Bauernvormarsch.
class BuiltinEngine : public Engine {
public:
    std::string name() const override { return "ChessEngine Builtin"; }
    bool supportsPosition(const Position&) const override { return true; }
    SearchResult search(const Position& pos, const SearchLimits& limits) override;

    // Statische Bewertung aus Sicht der Seite am Zug (oeffentlich fuer Tests/Plugins).
    int evaluate(const Position& pos) const;

private:
    int negamax(Position& pos, int depth, int alpha, int beta, int ply);
    // Variante mit Plugin-Regeln: rechnet auf einer Kopie der Partie (play/undo durchlaufen alle Hooks)
    SearchResult searchGame(const Game& game, const SearchLimits& limits);
    int negamaxGame(Game& g, int depth, int alpha, int beta, int ply);
    int quiesceGame(Game& g, int alpha, int beta, int ply);
    int quiesce(Position& pos, int alpha, int beta, int ply);
    void orderMoves(const Position& pos, std::vector<Move>& moves) const;
    bool checkAbort();

    uint64_t nodes_ = 0;
    uint64_t nodeLimit_ = 0;
    bool hasDeadline_ = false;
    std::chrono::steady_clock::time_point deadline_;
    const std::atomic<bool>* stop_ = nullptr;
    bool aborted_ = false;
    bool canAbort_ = false;
};

}  // namespace chess
