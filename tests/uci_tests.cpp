// Testet den UCI-Client gegen unser eigenes UCI-Programm (chessengine_uci).
// Mit Stockfish laeuft derselbe Code, nur mit anderem Pfad.

#include "framework.hpp"

#include "core/fen.hpp"
#include "core/notation.hpp"
#include "engine/uci_engine.hpp"

using namespace chess;

TEST_CASE(uci_handshake_and_mate_search) {
    std::string err;
    auto e = UciEngine::launch(CHESS_UCI_EXE, err);
    CHECK(e != nullptr);
    if (!e) {
        std::cerr << "  " << err << "\n";
        return;
    }
    CHECK_EQ(e->name(), std::string("ChessEngine Builtin"));
    CHECK(e->hasOption("MultiPV"));

    Position pos = fromFen(PieceRegistry::standard(), "6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
    SearchLimits lim;
    lim.depth = 3;
    const SearchResult r = e->search(pos, lim);
    CHECK(r.ok);
    CHECK_EQ(toUci(pos, r.bestMove), std::string("a1a8"));
    CHECK(!r.lines.empty() && r.lines.front().score.mate && r.lines.front().score.winning);
}

TEST_CASE(uci_multipv_and_searchmoves) {
    std::string err;
    auto e = UciEngine::launch(CHESS_UCI_EXE, err);
    CHECK(e != nullptr);
    if (!e) return;

    Position pos = fromFen(PieceRegistry::standard(), kStandardStartFen);
    SearchLimits lim;
    lim.depth = 2;
    lim.multiPv = 3;
    const SearchResult r = e->search(pos, lim);
    CHECK(r.ok);
    CHECK_EQ(r.lines.size(), size_t{3});

    lim.multiPv = 1;
    lim.rootMoves = {*parseMove(pos, "h3")};
    const SearchResult only = e->search(pos, lim);
    CHECK_EQ(toSan(pos, only.bestMove), std::string("h3"));
}

TEST_CASE(uci_rejects_variants_and_bad_paths) {
    std::string err;
    CHECK(UciEngine::launch("C:/gibt/es/nicht/stockfish.exe", err) == nullptr);
    CHECK(!err.empty());

    auto e = UciEngine::launch(CHESS_UCI_EXE, err);
    CHECK(e != nullptr);
    if (!e) return;
    Position big(PieceRegistry::standard(), 10, 10);
    CHECK(!e->supportsPosition(big));
    CHECK(!e->search(big, SearchLimits{}).ok);
}
