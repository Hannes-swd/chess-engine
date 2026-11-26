#pragma once

#include <cstdint>
#include <string>

namespace chess {

enum class Color : uint8_t { White = 0, Black = 1 };

inline Color opposite(Color c) { return c == Color::White ? Color::Black : Color::White; }

// Ein Feld ist ein Index rank * width + file. Das Brett ist W x H, nicht fest 8 x 8.
using Square = int;
constexpr Square NoSquare = -1;

// Index in die PieceRegistry.
using PieceTypeId = int;
constexpr PieceTypeId NoPieceType = -1;

struct Piece {
    PieceTypeId type = NoPieceType;
    Color color = Color::White;

    bool empty() const { return type == NoPieceType; }
    bool operator==(const Piece& o) const { return type == o.type && (type == NoPieceType || color == o.color); }
};

enum MoveFlag : uint8_t {
    FlagNone = 0,
    FlagCapture = 1,
    FlagEnPassant = 2,
    FlagCastle = 4,
    FlagDoublePush = 8,
    FlagPromotion = 16,
};

struct Move {
    Square from = NoSquare;
    Square to = NoSquare;
    PieceTypeId promotion = NoPieceType;
    uint8_t flags = FlagNone;
    // Nur bei Rochade gesetzt.
    Square rookFrom = NoSquare;
    Square rookTo = NoSquare;

    bool valid() const { return from != NoSquare; }
    bool operator==(const Move& o) const { return from == o.from && to == o.to && promotion == o.promotion; }
};

enum class GameResult : uint8_t { Ongoing, WhiteWins, BlackWins, Draw };

struct GameOutcome {
    GameResult result = GameResult::Ongoing;
    std::string reason;

    bool over() const { return result != GameResult::Ongoing; }
};

}  // namespace chess
