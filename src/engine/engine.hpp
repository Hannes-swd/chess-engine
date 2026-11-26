#pragma once

#include "core/game.hpp"
#include "core/position.hpp"
#include "engine/score.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace chess {

struct SearchLimits {
    int depth = 0;       // 0 = egal
    int movetimeMs = 0;  // 0 = egal
    uint64_t nodes = 0;  // 0 = egal
    int multiPv = 1;     // so viele beste Zuege mit Bewertung zurueckgeben
    std::vector<Move> rootMoves;               // leer = alle legalen Zuege
    const std::atomic<bool>* stop = nullptr;  // optional: von aussen abbrechen
};

struct ScoredMove {
    Move move;
    Score score;  // aus Sicht der Seite am Zug
    int depth = 0;
    std::vector<Move> pv;  // Hauptvariante, beginnt mit `move`
};

struct SearchResult {
    bool ok = false;
    std::string error;
    Move bestMove;                  // der Zug, den die Engine tatsaechlich spielen wuerde
    std::vector<ScoredMove> lines;  // beste zuerst (MultiPV)
    uint64_t nodes = 0;
};

// Gemeinsame Schnittstelle fuer Stockfish (UCI) und die eingebaute Engine.
class Engine {
public:
    virtual ~Engine() = default;
    virtual std::string name() const = 0;
    // Kann die Engine diese Stellung ueberhaupt verstehen? (Stockfish: nur 8x8 Standardfiguren)
    virtual bool supportsPosition(const Position& pos) const = 0;
    virtual SearchResult search(const Position& pos, const SearchLimits& limits) = 0;
    // Spielstaerke: skill 0..20 (20 = voll), elo > 0 begrenzt auf diese Elo. Engines ohne Support ignorieren das.
    virtual void configureStrength(int /*skill*/, int /*elo*/) {}
    virtual void newGame() {}
};

// 8x8 und nur die sechs Standardfiguren ohne eigene Zugfunktionen.
bool isStandardChessPosition(const Position& pos);
// Zusaetzlich keine Regel-Hooks: dann darf Stockfish die Partie bewerten.
bool usesStandardRules(const Game& game);

}  // namespace chess
