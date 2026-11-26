#pragma once

#include "core/position.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chess {

// Standard-Algebraische Notation ("Nf3", "exd6", "O-O", "e8=Q#").
// `pos` ist die Stellung VOR dem Zug; sie wird intern kurz veraendert und wiederhergestellt.
std::string toSan(Position& pos, const Move& m);

// Koordinatennotation wie bei UCI: "e2e4", "e7e8q".
std::string toUci(const Position& pos, const Move& m);

// Erkennt SAN ("Nf3", "0-0", "exd6+") und Koordinaten ("g1f3").
std::optional<Move> parseMove(Position& pos, const std::vector<Move>& candidates, std::string_view text);
std::optional<Move> parseMove(Position& pos, std::string_view text);

// Nur Koordinatennotation ("e2e4", "e7e8q"); schneller als parseMove, fuer Engine-Ausgaben.
std::optional<Move> parseUciMove(Position& pos, std::string_view text);

}  // namespace chess
