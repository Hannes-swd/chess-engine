#pragma once

#include "core/types.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chess {

class Position;

enum class StepMode : uint8_t { MoveOrCapture, MoveOnly, CaptureOnly };

// Ein Zugschritt aus Sicht von Weiss: dy > 0 heisst "nach vorne".
// Fuer Schwarz wird dy automatisch gespiegelt.
struct Step {
    int dx = 0;
    int dy = 0;
    bool slide = false;  // true: gleitet bis zum naechsten Hindernis (Turm, Laeufer ...)
    StepMode mode = StepMode::MoveOrCapture;
};

// Fuer Figuren, deren Zuege sich nicht als Schritte beschreiben lassen (z. B. aus Lua-Plugins).
// Erzeugt pseudo-legale Zuege der Figur auf `from`; Legalitaet (eigener Koenig im Schach) prueft der Kern.
using CustomMoveFn = std::function<void(const Position&, Square from, std::vector<Move>& out)>;

struct PieceType {
    std::string id;     // eindeutig, z. B. "knight" oder "drache"
    std::string name;   // Anzeigename
    char symbol = '?';  // Grossbuchstabe fuer FEN/SAN
    int value = 0;      // Materialwert in Centipawns
    std::vector<Step> steps;

    bool royal = false;            // Schach/Matt bezieht sich auf diese Figur
    bool pawnLike = false;         // Doppelschritt, en passant, Umwandlung
    bool castles = false;          // darf rochieren
    bool castlePartner = false;    // Rochadepartner (Turm)
    bool promotionTarget = false;  // Bauern duerfen sich in diese Figur umwandeln

    CustomMoveFn customMoves;  // optional, zusaetzlich zu `steps`
};

class PieceRegistry {
public:
    // Fuegt einen Typ hinzu. Existiert die id schon, wird er ersetzt.
    PieceTypeId add(PieceType type);

    const PieceType& get(PieceTypeId id) const { return types_[static_cast<size_t>(id)]; }
    PieceType& edit(PieceTypeId id) { return types_[static_cast<size_t>(id)]; }

    std::optional<PieceTypeId> findById(const std::string& id) const;
    std::optional<PieceTypeId> findBySymbol(char upperSymbol) const;

    int size() const { return static_cast<int>(types_.size()); }
    const std::vector<PieceType>& all() const { return types_; }

    // Bauer, Springer, Laeufer, Turm, Dame, Koenig.
    static std::shared_ptr<PieceRegistry> standard();

private:
    std::vector<PieceType> types_;
};

// Bausteine fuer eigene Figuren.
namespace steps {
std::vector<Step> orthogonal(bool slide);
std::vector<Step> diagonal(bool slide);
std::vector<Step> knight();
std::vector<Step> combine(std::vector<Step> a, const std::vector<Step>& b);
}  // namespace steps

}  // namespace chess
