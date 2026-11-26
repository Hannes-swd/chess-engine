#pragma once

#include "engine/bot.hpp"

#include <string>

namespace chess {

// Lokale Elo-Wertung des Spielers (nur Partien gegen Bots zaehlen).
struct PlayerProfile {
    std::string name = "Du";
    int rating = 1200;
    int peak = 1200;
    int games = 0;
    int wins = 0;
    int draws = 0;
    int losses = 0;
    int lastDelta = 0;
};

// Erwartete Punktzahl (0..1) von a gegen b, FIDE-Formel.
double expectedScore(int a, int b);

// Elo-Aenderung fuer `score` (1 Sieg, 0.5 Remis, 0 Niederlage). K = 40 in den ersten 30 Partien, danach 20.
int eloDelta(int rating, int opponent, double score, int gamesPlayed);

// Wertet die Partie und aktualisiert das Profil. Gibt die Aenderung zurueck.
int applyResult(PlayerProfile& p, int opponentElo, double score);

// Grobe Schaetzung, wie stark ein Bot-Profil spielt (fuer Anzeige und Wertung).
// Mit Stockfish: Elo-Begrenzung bzw. Stufe; ohne: eingebaute Engine (~1100).
// Patzer, Zufall und eine grosse Toleranz machen den Bot schwaecher.
int estimatedBotElo(const BotProfile& p, bool withStockfish);

std::string playerProfileToJson(const PlayerProfile& p);
bool playerProfileFromJson(const std::string& text, PlayerProfile& p);

}  // namespace chess
