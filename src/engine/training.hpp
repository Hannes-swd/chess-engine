#pragma once

// Lernen aus den eigenen Partien:
//  - Aufgaben: jeder eigene Fehler/Patzer aus der Analyse wird zu "Finde den besseren Zug".
//    Wiederholung nach dem Karteikasten-Prinzip: richtig -> spaeter wieder, falsch -> bald wieder.
//  - Fehlerarten ("Figur eingestellt", "Matt uebersehen" ...) fuer die Statistik.
//  - Statistik aus gespeicherten PGN-Dateien (Bewertungen stehen in den Kommentaren).

#include "analysis/analysis.hpp"
#include "core/pgn.hpp"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace chess {

struct Puzzle {
    std::string fen;       // Stellung vor dem Fehler
    std::string bestUci;   // gesuchter Zug
    std::string bestSan;
    std::string playedSan;  // was in der Partie gespielt wurde
    std::string theme;      // Fehlerart
    std::string source;     // "Du – Clubspieler, 26.11.2026"
    MoveQuality quality = MoveQuality::Mistake;
    int box = 0;            // Karteikasten-Fach 0..5
    long long due = 0;      // faellig ab (Unix-Zeit, Sekunden)
    int solved = 0, failed = 0;

    std::string key() const { return fen + "|" + bestUci; }
};

struct TrainingData {
    std::vector<Puzzle> puzzles;
    std::map<std::string, int> openingRuns;   // Eroeffnung -> so oft geuebt
    std::map<std::string, int> openingClean;  // ... davon fehlerfrei
};

// Einordnung eines Fehlers in eine verstaendliche Fehlerart.
std::string mistakeTheme(const Game& game, const MoveReview& review);

// Fehler, Patzer und verpasste Chancen von `side` (oder beiden Seiten) als Aufgaben.
std::vector<Puzzle> puzzlesFromAnalysis(const Game& game, const GameAnalysis& analysis, std::optional<Color> side,
                                        const std::string& source, long long now);

// Neue Aufgaben uebernehmen (doppelte werden uebersprungen). Gibt die Zahl der neuen zurueck.
int mergePuzzles(TrainingData& data, const std::vector<Puzzle>& fresh);

// Ergebnis eintragen: richtig -> naechstes Fach (1, 3, 7, 21, 60 Tage), falsch -> Fach 0 (in 10 Minuten).
void schedule(Puzzle& p, bool correct, long long now);

// Indizes der faelligen Aufgaben, aelteste zuerst.
std::vector<size_t> duePuzzles(const TrainingData& data, long long now);

std::string trainingToJson(const TrainingData& data);
bool trainingFromJson(const std::string& text, TrainingData& data);

// --- Statistik aus gespeicherten Partien -------------------------------------------

// Bewertungen aus PGN-Kommentaren ("[%eval 0.35]") -> Genauigkeit von `side` (0..100), nullopt ohne Bewertungen.
std::optional<double> accuracyFromComments(const PgnRecord& rec, Color side);

// Wie oft kam welche Zugkategorie bei `side` vor (aus den Kommentaren "Patzer", "Fehler" ...).
std::array<int, kQualityCount> qualityCountsFromComments(const PgnRecord& rec, Color side);

}  // namespace chess
