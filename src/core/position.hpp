#pragma once

#include "core/piece_type.hpp"
#include "core/types.hpp"

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace chess {

// Eine Stellung auf einem beliebig grossen W x H Brett.
// Alle Regeln (Zugmuster, Rochade, en passant, Umwandlung) kommen aus der PieceRegistry,
// nichts ist auf 8 x 8 oder die Standardfiguren festgelegt.
class Position {
public:
    struct Undo {
        Piece moved;
        Piece captured;
        Square capturedSq = NoSquare;
        Square prevEnPassant = NoSquare;
        int prevHalfmove = 0;
        std::array<std::pair<Square, uint8_t>, 5> unmovedSaved{};
        int unmovedCount = 0;
    };

    explicit Position(std::shared_ptr<const PieceRegistry> registry, int width = 8, int height = 8);

    // --- Geometrie ---------------------------------------------------------
    int width() const { return width_; }
    int height() const { return height_; }
    int squareCount() const { return width_ * height_; }
    Square square(int file, int rank) const { return rank * width_ + file; }
    int fileOf(Square s) const { return s % width_; }
    int rankOf(Square s) const { return s / width_; }
    bool contains(int file, int rank) const { return file >= 0 && file < width_ && rank >= 0 && rank < height_; }

    // Leert das Brett und setzt eine neue Groesse.
    void resize(int width, int height);

    // --- Inhalt ------------------------------------------------------------
    const Piece& at(Square s) const { return board_[static_cast<size_t>(s)]; }
    void place(Square s, Piece p, bool unmoved = false);
    void clear(Square s);

    Color sideToMove() const { return side_; }
    void setSideToMove(Color c) { side_ = c; }
    Square enPassant() const { return enPassant_; }
    void setEnPassant(Square s) { enPassant_ = s; }
    int halfmoveClock() const { return halfmove_; }
    void setHalfmoveClock(int v) { halfmove_ = v; }
    int fullmoveNumber() const { return fullmove_; }
    void setFullmoveNumber(int v) { fullmove_ = v; }

    // "Unbewegt"-Markierung: entscheidet ueber Rochaderechte.
    bool isUnmoved(Square s) const { return unmoved_[static_cast<size_t>(s)] != 0; }
    void setUnmoved(Square s, bool v) { unmoved_[static_cast<size_t>(s)] = v ? 1 : 0; }

    const PieceRegistry& registry() const { return *registry_; }
    const std::shared_ptr<const PieceRegistry>& registryPtr() const { return registry_; }

    int forward(Color c) const { return c == Color::White ? 1 : -1; }
    int pawnStartRank(Color c) const { return c == Color::White ? 1 : height_ - 2; }
    int promotionRank(Color c) const { return c == Color::White ? height_ - 1 : 0; }

    // --- Zuege -------------------------------------------------------------
    void generatePseudoLegal(std::vector<Move>& out) const;
    // Schnell, nutzt make/unmake auf dieser Stellung (bleibt danach unveraendert).
    void generateLegal(std::vector<Move>& out);
    // Bequem fuer const-Kontexte, arbeitet auf einer Kopie.
    std::vector<Move> legalMoves() const;

    bool isAttacked(Square s, Color by) const;
    bool inCheck(Color c) const;
    std::vector<Square> royalSquares(Color c) const;

    Undo makeMove(const Move& m);
    void unmakeMove(const Move& m, const Undo& u);

    // Schluessel fuer Stellungswiederholung.
    std::string repetitionKey() const;

private:
    void generateSteps(Square from, const Piece& p, const PieceType& t, std::vector<Move>& out) const;
    void generateCastling(Square from, const Piece& p, std::vector<Move>& out) const;
    void addMove(Square from, Square to, uint8_t flags, const Piece& p, const PieceType& t,
                 std::vector<Move>& out) const;

    std::shared_ptr<const PieceRegistry> registry_;
    int width_ = 8;
    int height_ = 8;
    std::vector<Piece> board_;
    std::vector<uint8_t> unmoved_;
    Color side_ = Color::White;
    Square enPassant_ = NoSquare;
    int halfmove_ = 0;
    int fullmove_ = 1;
};

}  // namespace chess
