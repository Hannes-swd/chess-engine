#pragma once

#include "core/game.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chess {

using PgnTags = std::vector<std::pair<std::string, std::string>>;

std::string resultToString(GameResult r);

// Exportiert die Partie als PGN-Text (kostenlos in jedem Analyse-Tool einlesbar).
// `tags` ueberschreibt oder ergaenzt die Standard-Tags (Event, White, Black, Date ...).
// `moveComments` (optional): ein Kommentar pro Halbzug, z. B. "[%eval 0.35] Ungenau". Leere werden ausgelassen.
std::string toPgn(const Game& game, const PgnTags& tags = {}, const std::vector<std::string>* moveComments = nullptr);

// Eine eingelesene Partie: Tags und die Zuege der Hauptvariante (SAN, ohne Nummern und Kommentare).
struct PgnRecord {
    PgnTags tags;
    std::vector<std::string> moves;
    std::vector<std::string> comments;  // Kommentar direkt nach jedem Zug (leer, wenn keiner), z. B. "[%eval 0.3] Gut"
    std::string tag(const std::string& key) const;  // leer, wenn nicht vorhanden
};

// Liest die erste Partie eines PGN-Texts. Kommentare {..} und ;.., Varianten (..), NAGs ($1) und
// Bewertungszeichen (!?, ??) werden uebersprungen. nullopt, wenn weder Tags noch Zuege gefunden wurden.
std::optional<PgnRecord> parsePgn(std::string_view text);

// Spielt die Zuege nach (Standardschach, eigene Startstellung ueber den FEN-Tag) und uebernimmt ein
// Ergebnis wie "1-0", das sich nicht aus der Stellung ergibt (Aufgabe, Zeit). Wirft bei ungueltigem Zug.
Game gameFromPgn(const PgnRecord& record);

}  // namespace chess
