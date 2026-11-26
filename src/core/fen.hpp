#pragma once

#include "core/position.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace chess {

// FEN funktioniert auch fuer andere Brettgroessen: "10/10/..." (mehrstellige Zahlen sind erlaubt)
// und fuer eigene Figuren, solange ihr Symbol in der Registry eindeutig ist.
// Wirft std::invalid_argument bei ungueltigem Text.
Position fromFen(std::shared_ptr<const PieceRegistry> registry, std::string_view fen);
std::string toFen(const Position& pos);

// "a1", "e4", "j10" ...
std::string squareName(const Position& pos, Square s);
std::optional<Square> parseSquare(const Position& pos, std::string_view text);

}  // namespace chess
