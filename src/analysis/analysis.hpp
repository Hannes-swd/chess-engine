#pragma once

#include "core/game.hpp"
#include "engine/engine.hpp"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace chess {

// Zugkategorien wie in grossen Analyse-Tools, von besonders gut bis sehr schlecht.
enum class MoveQuality { Brilliant, Great, Best, Excellent, Good, Inaccuracy, Mistake, Miss, Blunder, Forced };
constexpr int kQualityCount = 10;

// Grenzen fuer die Einstufung. Gemessen wird der Verlust an Gewinnchance (Prozentpunkte 0..100),
// so zaehlt ein Fehler in ausgeglichener Stellung mehr als in einer laengst gewonnenen.
struct QualityThresholds {
    double excellent = 2.0;
    double good = 5.0;
    double inaccuracy = 10.0;
    double mistake = 20.0;     // darueber: Patzer
    int bestTolerance = 10;    // cp: so nah am besten Zug gilt als "Bester Zug"
    int clamp = 1500;          // cp: Bewertungen werden fuer den Verlust begrenzt (Matt = clamp)
    int greatGap = 150;        // cp: Abstand zum zweitbesten Zug fuer "Sehr gut"
    int sacrifice = 200;       // cp: so viel Material muss geopfert sein fuer "Brillant"
};

struct MoveReview {
    int ply = 0;
    Color mover = Color::White;
    std::string san;
    Move played;
    Move best;
    std::string bestSan;
    Score bestScore;                  // beste Bewertung vor dem Zug, aus Sicht des Ziehenden
    Score playedScore;                // Bewertung nach dem gespielten Zug, aus Sicht des Ziehenden
    std::optional<Score> secondScore; // zweitbester Zug (fuer "Sehr gut")
    int cpLoss = 0;
    double winBefore = 50;  // Gewinnchance des Ziehenden vor dem Zug (0..100)
    double winAfter = 50;   // ... nach dem gespielten Zug
    double accuracy = 100;  // Genauigkeit dieses Zuges (0..100)
    MoveQuality quality = MoveQuality::Best;
    bool sacrifice = false;

    std::vector<Move> bestLine;             // beste Fortsetzung ab der Stellung vor dem Zug
    std::vector<std::string> bestLineSan;
    std::vector<Move> threatLine;           // beste Antwort des Gegners ab der Stellung nach dem Zug
    std::vector<std::string> threatLineSan;

    std::string headline;     // "Nxd5 ist ungenau"
    std::string explanation;  // ein paar Saetze: warum, was droht, was besser war
    std::string headlineEn;   // dasselbe auf Englisch
    std::string explanationEn;
};

struct PlayerSummary {
    int moves = 0;
    double accuracy = 0;  // Durchschnitt der Zuggenauigkeiten
    int avgCpLoss = 0;
    std::array<int, kQualityCount> counts{};
};

struct GameAnalysis {
    bool ok = false;
    std::string error;
    std::string engineName;
    std::vector<MoveReview> moves;
    std::vector<Score> whiteEvals;  // Bewertung jeder Stellung 0..n aus Sicht von Weiss
    PlayerSummary white, black;
    // Anteil der Stellungen (in %), in denen eine Seite klar besser stand (Gewinnchance > 60 %)
    double whiteBetter = 0, balanced = 0, blackBetter = 0;

    const PlayerSummary& summary(Color c) const { return c == Color::White ? white : black; }
};

using AnalysisProgress = std::function<void(int done, int total)>;

// Bewertet jede Stellung der Partie (zwei beste Zuege) und klassifiziert und erklaert jeden Zug.
GameAnalysis analyseGame(const Game& game, Engine& engine, const SearchLimits& limits,
                         const QualityThresholds& thresholds = {}, const AnalysisProgress& progress = {});

// Gewinnchance in % aus Sicht der Seite, zu der die Bewertung gehoert (Lichess-Formel).
double winChance(const Score& s);
// Genauigkeit eines Zuges aus dem Verlust an Gewinnchance (Lichess-Formel).
double moveAccuracy(double winBefore, double winAfter);

std::string qualityName(MoveQuality q);  // "Brillant", "Sehr gut", "Bester Zug", ...
bool isError(MoveQuality q);             // Ungenau, Fehler, Verpasst, Patzer

// Zugfolge als Text mit Zugnummern: "12. Sf3 Sc6 13. Lb5".
std::string lineText(const Position& start, const std::vector<Move>& moves, size_t maxPlies = 6);

// Ein PGN-Kommentar pro Halbzug: "[%eval 0.35] Ungenau – besser: Nf3".
std::vector<std::string> pgnComments(const GameAnalysis& analysis);

// Lesbare Uebersicht fuer Konsole oder Textdatei.
std::string formatAnalysis(const Game& game, const GameAnalysis& analysis);

}  // namespace chess
