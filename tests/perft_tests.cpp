// Perft zaehlt alle legalen Zugfolgen bis zu einer Tiefe. Die Sollwerte sind die bekannten
// Referenzzahlen (Chess Programming Wiki). Stimmen sie, ist die Zuggenerierung korrekt.

#include "framework.hpp"

#include "core/fen.hpp"
#include "core/game.hpp"
#include "core/position.hpp"

#include <cstdint>

using namespace chess;

namespace {

uint64_t perft(Position& pos, int depth) {
    std::vector<Move> moves;
    pos.generateLegal(moves);
    if (depth == 1) return moves.size();
    uint64_t n = 0;
    for (const Move& m : moves) {
        const auto u = pos.makeMove(m);
        n += perft(pos, depth - 1);
        pos.unmakeMove(m, u);
    }
    return n;
}

uint64_t perftFen(const char* fen, int depth) {
    Position pos = fromFen(PieceRegistry::standard(), fen);
    const std::string before = toFen(pos);
    const uint64_t n = perft(pos, depth);
    CHECK_EQ(toFen(pos), before);  // make/unmake muss die Stellung exakt wiederherstellen
    return n;
}

}  // namespace

TEST_CASE(perft_startposition) {
    CHECK_EQ(perftFen(kStandardStartFen, 1), 20ull);
    CHECK_EQ(perftFen(kStandardStartFen, 2), 400ull);
    CHECK_EQ(perftFen(kStandardStartFen, 3), 8902ull);
    CHECK_EQ(perftFen(kStandardStartFen, 4), 197281ull);
    CHECK_EQ(perftFen(kStandardStartFen, 5), 4865609ull);
}

TEST_CASE(perft_kiwipete) {
    const char* fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    CHECK_EQ(perftFen(fen, 1), 48ull);
    CHECK_EQ(perftFen(fen, 2), 2039ull);
    CHECK_EQ(perftFen(fen, 3), 97862ull);
    CHECK_EQ(perftFen(fen, 4), 4085603ull);
}

TEST_CASE(perft_position3_en_passant_pins) {
    const char* fen = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
    CHECK_EQ(perftFen(fen, 1), 14ull);
    CHECK_EQ(perftFen(fen, 2), 191ull);
    CHECK_EQ(perftFen(fen, 3), 2812ull);
    CHECK_EQ(perftFen(fen, 4), 43238ull);
    CHECK_EQ(perftFen(fen, 5), 674624ull);
}

TEST_CASE(perft_position4_promotions) {
    const char* fen = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
    CHECK_EQ(perftFen(fen, 1), 6ull);
    CHECK_EQ(perftFen(fen, 2), 264ull);
    CHECK_EQ(perftFen(fen, 3), 9467ull);
    CHECK_EQ(perftFen(fen, 4), 422333ull);
}

TEST_CASE(perft_position5) {
    const char* fen = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";
    CHECK_EQ(perftFen(fen, 1), 44ull);
    CHECK_EQ(perftFen(fen, 2), 1486ull);
    CHECK_EQ(perftFen(fen, 3), 62379ull);
    CHECK_EQ(perftFen(fen, 4), 2103487ull);
}
