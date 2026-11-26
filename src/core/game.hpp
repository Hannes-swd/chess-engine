#pragma once

#include "core/hooks.hpp"
#include "core/piece_type.hpp"
#include "core/position.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chess {

extern const char* const kStandardStartFen;

// Eine Variante = Figurensatz + Startstellung + Regel-Hooks.
// Standardschach ist einfach die Variante ohne zusaetzliche Hooks.
struct Variant {
    std::string name = "Standard";
    std::shared_ptr<PieceRegistry> pieces;  // nullptr -> Standardfiguren
    std::string startFen;                   // leer -> Standard-Startstellung
    RuleHooks hooks;
    // Matt, Patt, 50 Zuege, dreifache Wiederholung, ungenuegendes Material.
    bool standardEndRules = true;
};

Variant standardVariant();

// Hat diese Farbe noch genug Material, um ueberhaupt mattzusetzen?
// (Fuer Zeitueberschreitung: laeuft die Zeit ab und der Gegner kann nicht mehr matt setzen, ist es Remis.)
bool hasMatingMaterial(const Position& pos, Color c);

class Game {
public:
    explicit Game(Variant variant = standardVariant());

    // Startet die Partie neu (FEN laden, onSetup-Hooks ausfuehren).
    void reset();

    const Position& position() const { return current_; }
    const Position& startPosition() const { return start_; }
    const Variant& variant() const { return variant_; }

    // Legale Zuege nach Anwendung aller filterMoves-Hooks.
    std::vector<Move> legalMoves() const;

    bool play(const Move& m);
    bool play(std::string_view sanOrUci);
    bool undo();

    GameOutcome outcome() const;
    // Ergebnis von aussen festlegen: Aufgabe, Zeitueberschreitung, Remis-Einigung.
    // Wird durch undo() und reset() wieder aufgehoben.
    void setResult(GameOutcome result) { declared_ = std::move(result); }

    int ply() const { return static_cast<int>(moves_.size()); }
    // Stellung vor Halbzug `ply` (0 = Start, ply() = aktuelle Stellung).
    const Position& positionAt(int ply) const;
    const std::vector<Move>& moves() const { return moves_; }
    const std::vector<std::string>& sanMoves() const { return san_; }
    // Wie oft die aktuelle Stellung schon vorkam (inklusive jetzt).
    int repetitionCount() const;

private:
    Variant variant_;
    Position start_;
    Position current_;
    std::vector<Position> history_;  // Stellungen vor jedem Zug (Plugins duerfen Stellungen frei aendern)
    std::vector<Move> moves_;
    std::vector<std::string> san_;
    std::vector<std::string> keys_;
    std::optional<GameOutcome> declared_;
};

}  // namespace chess
