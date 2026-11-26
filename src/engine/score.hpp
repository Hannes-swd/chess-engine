#pragma once

#include <string>

namespace chess {

// Bewertung immer aus Sicht der Seite am Zug.
// Entweder Centipawns (100 = ein Bauer) oder "Matt in N Zuegen".
struct Score {
    bool mate = false;
    bool winning = false;  // nur bei mate: true = wir setzen matt, false = wir werden matt gesetzt
    int value = 0;         // Centipawns, oder bei mate die Anzahl Zuege bis Matt (0 = schon matt)

    static Score centipawns(int cp);
    static Score mateIn(int moves, bool winning);

    // Dieselbe Stellung aus Sicht des Gegners.
    Score negated() const;

    // Vergleichswert: jedes Gewinnmatt > jede Centipawn-Zahl > jedes Verlustmatt.
    long long order() const;

    // Matt zaehlt als +/-limit, sonst auf [-limit, limit] begrenzt. Fuer Verlustrechnung in der Analyse.
    int clamped(int limit) const;

    std::string toString() const;  // "+0.35", "-1.20", "#3", "#-2"
    std::string toPgnEval() const;  // "0.35", "-1.20", "#3", "#-2" (fuer [%eval ...])
};

}  // namespace chess
