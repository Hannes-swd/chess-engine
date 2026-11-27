#include "framework.hpp"

#include "core/fen.hpp"
#include "core/game.hpp"
#include "core/notation.hpp"
#include "core/pgn.hpp"

#include <algorithm>
#include <string>

using namespace chess;

namespace {

Variant variantFromFen(const std::string& fen) {
    Variant v = standardVariant();
    v.startFen = fen;
    return v;
}

std::vector<std::string> allSan(const Game& g) {
    Position tmp = g.position();
    std::vector<std::string> out;
    for (const Move& m : g.legalMoves()) out.push_back(toSan(tmp, m));
    return out;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

TEST_CASE(fen_roundtrip) {
    const char* fens[] = {
        kStandardStartFen,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 b - - 13 42",
    };
    for (const char* f : fens) CHECK_EQ(toFen(fromFen(PieceRegistry::standard(), f)), std::string(f));
}

TEST_CASE(san_basics_and_disambiguation) {
    Game g;
    const auto san = allSan(g);
    CHECK(contains(san, "e4"));
    CHECK(contains(san, "Nf3"));
    CHECK_EQ(san.size(), size_t{20});

    Game k(variantFromFen("4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1"));
    const auto ks = allSan(k);
    CHECK(contains(ks, "Nbd2"));
    CHECK(contains(ks, "Nfd2"));
}

TEST_CASE(castling_both_sides) {
    Game g(variantFromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    const auto san = allSan(g);
    CHECK(contains(san, "O-O"));
    CHECK(contains(san, "O-O-O"));
    CHECK(g.play("O-O"));
    CHECK_EQ(toFen(g.position()), std::string("r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1"));
    CHECK(g.play("0-0-0"));
    CHECK_EQ(toFen(g.position()), std::string("2kr3r/8/8/8/8/8/8/R4RK1 w - - 2 2"));
}

TEST_CASE(en_passant) {
    Game g(variantFromFen("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3"));
    CHECK(contains(allSan(g), "exf6"));
    CHECK(g.play("exf6"));
    CHECK(g.position().at(*parseSquare(g.position(), "f5")).empty());
}

TEST_CASE(promotion) {
    Game g(variantFromFen("8/4P3/8/8/8/8/k7/4K3 w - - 0 1"));
    const auto san = allSan(g);
    CHECK_EQ(san.size(), size_t{9});
    CHECK(contains(san, "e8=Q"));
    CHECK(contains(san, "e8=N"));
    CHECK(g.play("e8=Q"));
    CHECK_EQ(g.position().registry().get(g.position().at(*parseSquare(g.position(), "e8")).type).id,
             std::string("queen"));
}

TEST_CASE(checkmate_fools_mate_and_pgn) {
    Game g;
    for (const char* m : {"f3", "e5", "g4", "Qh4"}) CHECK(g.play(m));
    CHECK_EQ(g.sanMoves().back(), std::string("Qh4#"));
    const auto out = g.outcome();
    CHECK(out.result == GameResult::BlackWins);
    CHECK_EQ(out.reason, std::string("Schachmatt"));
    CHECK(!g.play("e4"));  // nach Matt keine Zuege mehr

    const std::string pgn = toPgn(g, {{"White", "Anna"}});
    CHECK(pgn.find("1. f3 e5 2. g4 Qh4# 0-1") != std::string::npos);
    CHECK(pgn.find("[White \"Anna\"]") != std::string::npos);
    CHECK(pgn.find("[Result \"0-1\"]") != std::string::npos);
}

TEST_CASE(pgn_roundtrip_and_import) {
    // Eigene Ausgabe mit Bewertungs-Kommentaren wieder einlesen
    Game g;
    for (const char* m : {"e4", "e5", "Nf3", "Nc6", "Bb5"}) CHECK(g.play(m));
    const std::vector<std::string> comments = {"[%eval 0.30]", "", "[%eval 0.25] Gut", "", ""};
    const auto rec = parsePgn(toPgn(g, {{"White", "Anna \"A.\""}}, &comments));
    CHECK(rec.has_value());
    CHECK_EQ(rec->moves.size(), size_t{5});
    CHECK_EQ(rec->tag("White"), std::string("Anna \"A.\""));
    const Game back = gameFromPgn(*rec);
    CHECK_EQ(back.ply(), 5);
    CHECK_EQ(back.sanMoves()[4], std::string("Bb5"));

    // Fremdes PGN: Zugnummern ohne Leerzeichen, Varianten, NAGs, Bewertungszeichen, Aufgabe
    const auto other = parsePgn(
        "[Event \"Test\"]\n[Result \"1-0\"]\n\n1.e4 e5 2.Nf3 (2.f4 exf4 {Koenigsgambit}) 2...Nc6 $1 "
        "3.Bc4!? Nf6?? ; Fehler\n4.Ng5 1-0\n\n[Event \"Zweite\"]\n1. d4 *");
    CHECK(other.has_value());
    CHECK_EQ(other->moves.size(), size_t{7});
    const Game og = gameFromPgn(*other);
    CHECK(og.outcome().result == GameResult::WhiteWins);  // Ergebnis aus der Datei, kein Matt
    CHECK_EQ(og.sanMoves()[6], std::string("Ng5"));

    bool threw = false;
    try {
        gameFromPgn(*parsePgn("1. e4 e5 2. Ke3"));
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(!parsePgn("   \n").has_value());
}

TEST_CASE(stalemate) {
    Game g(variantFromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
    const auto out = g.outcome();
    CHECK(out.result == GameResult::Draw);
    CHECK_EQ(out.reason, std::string("Patt"));
}

TEST_CASE(threefold_repetition) {
    Game g;
    for (const char* m : {"Nf3", "Nf6", "Ng1", "Ng8", "Nf3", "Nf6", "Ng1"}) CHECK(g.play(m));
    CHECK(!g.outcome().over());
    CHECK(g.play("Ng8"));
    CHECK_EQ(g.outcome().reason, std::string("Dreifache Stellungswiederholung"));
}

TEST_CASE(fifty_move_rule) {
    Game g(variantFromFen("4k3/8/8/8/8/8/8/R3K3 w - - 99 60"));
    CHECK(!g.outcome().over());
    CHECK(g.play("Ra2"));
    CHECK_EQ(g.outcome().reason, std::string("50-Züge-Regel"));
}

TEST_CASE(insufficient_material) {
    CHECK_EQ(Game(variantFromFen("4k3/8/8/8/8/8/8/4K3 w - - 0 1")).outcome().reason,
             std::string("Ungenügendes Material"));
    CHECK_EQ(Game(variantFromFen("4k3/8/8/8/8/8/8/2B1K3 w - - 0 1")).outcome().reason,
             std::string("Ungenügendes Material"));
    CHECK(!Game(variantFromFen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1")).outcome().over());
}

TEST_CASE(undo_restores_position) {
    Game g;
    CHECK(g.play("e4"));
    CHECK(g.play("e2e4") == false);  // Feld ist leer
    CHECK(g.play("c7c5"));
    CHECK(g.undo());
    CHECK(g.undo());
    CHECK(!g.undo());
    CHECK_EQ(toFen(g.position()), std::string(kStandardStartFen));
}

TEST_CASE(invalid_fen_throws) {
    bool threw = false;
    try {
        fromFen(PieceRegistry::standard(), "rnbqkbnr/ppppXppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}
