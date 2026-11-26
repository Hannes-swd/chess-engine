#pragma once

#include "core/game.hpp"

#include <string>
#include <utility>
#include <vector>

namespace chess {

using PgnTags = std::vector<std::pair<std::string, std::string>>;

std::string resultToString(GameResult r);

// Exportiert die Partie als PGN-Text (kostenlos in jedem Analyse-Tool einlesbar).
// `tags` ueberschreibt oder ergaenzt die Standard-Tags (Event, White, Black, Date ...).
// `moveComments` (optional): ein Kommentar pro Halbzug, z. B. "[%eval 0.35] Ungenau". Leere werden ausgelassen.
std::string toPgn(const Game& game, const PgnTags& tags = {}, const std::vector<std::string>* moveComments = nullptr);

}  // namespace chess
