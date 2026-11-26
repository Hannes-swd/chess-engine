// Tests fuer das "alles ist ueberschreibbar"-Modell: andere Brettgroessen, neue Figuren, Hooks.

#include "framework.hpp"

#include "core/fen.hpp"
#include "core/game.hpp"
#include "core/notation.hpp"

#include <algorithm>

using namespace chess;

namespace {

// Drache = Turm + Springer
PieceTypeId addDragon(PieceRegistry& reg) {
    PieceType d;
    d.id = "drache";
    d.name = "Drache";
    d.symbol = 'D';
    d.value = 800;
    d.steps = steps::combine(steps::orthogonal(true), steps::knight());
    d.promotionTarget = true;
    return reg.add(d);
}

}  // namespace

TEST_CASE(new_piece_on_10x10_board) {
    auto reg = PieceRegistry::standard();
    addDragon(*reg);

    Position pos = fromFen(reg, "10/10/10/10/10/4D5/10/10/10/10 w - - 0 1");
    CHECK_EQ(pos.width(), 10);
    CHECK_EQ(pos.height(), 10);
    CHECK_EQ(toFen(pos), std::string("10/10/10/10/10/4D5/10/10/10/10 w - - 0 1"));
    // 9 + 9 Turmzuege + 8 Springerspruenge
    CHECK_EQ(pos.legalMoves().size(), size_t{26});
}

TEST_CASE(new_piece_gives_check) {
    auto reg = PieceRegistry::standard();
    addDragon(*reg);
    // Drache auf c2 greift den Koenig auf d4 per Springersprung an
    Position pos = fromFen(reg, "8/8/8/8/3k4/8/2D5/4K3 b - - 0 1");
    CHECK(pos.inCheck(Color::Black));
}

TEST_CASE(custom_move_function) {
    // "Teleporter": darf auf jedes leere Feld springen (wie eine Lua-Funktion es koennte)
    auto reg = PieceRegistry::standard();
    PieceType t;
    t.id = "teleporter";
    t.name = "Teleporter";
    t.symbol = 'T';
    t.customMoves = [](const Position& pos, Square from, std::vector<Move>& out) {
        for (Square s = 0; s < pos.squareCount(); ++s)
            if (pos.at(s).empty()) out.push_back(Move{from, s, NoPieceType, FlagNone});
    };
    reg->add(t);

    Position pos = fromFen(reg, "4k3/8/8/8/8/8/8/T3K3 w - - 0 1");
    // 61 leere Felder + 5 Koenigszuege (d1, d2, e2, f2, f1)
    CHECK_EQ(pos.legalMoves().size(), size_t{66});
}

TEST_CASE(hook_filter_moves) {
    Variant v = standardVariant();
    v.name = "Ohne Doppelschritt";
    v.hooks.filterMoves.add("test", 0, [](const Position&, std::vector<Move>& moves) {
        std::erase_if(moves, [](const Move& m) { return (m.flags & FlagDoublePush) != 0; });
    });
    Game g(v);
    CHECK_EQ(g.legalMoves().size(), size_t{12});
    CHECK(!g.play("e4"));
    CHECK(g.play("e3"));
}

TEST_CASE(hook_setup_resizes_board) {
    Variant v = standardVariant();
    v.name = "Riesenbrett";
    v.hooks.onSetup.add("riesenbrett", 0, [](Position& p) {
        p.resize(10, 10);
        const auto king = *p.registry().findById("king");
        const auto rook = *p.registry().findById("rook");
        p.place(p.square(5, 0), Piece{king, Color::White});
        p.place(p.square(5, 9), Piece{king, Color::Black});
        p.place(p.square(0, 0), Piece{rook, Color::White});
    });
    Game g(v);
    CHECK_EQ(g.position().width(), 10);
    CHECK(g.play("Ra10"));
    CHECK_EQ(g.sanMoves().back(), std::string("Ra10+"));  // Turm gibt Schach auf Rang 10
}

TEST_CASE(hook_manipulates_after_move_and_custom_end) {
    Variant v = standardVariant();
    v.name = "Koenig des Huegels";
    // Wer mit dem Koenig ein Zentrumsfeld erreicht, gewinnt.
    v.hooks.checkGameEnd.add("koth", 10, [](const Game& g) -> std::optional<GameOutcome> {
        const Position& p = g.position();
        for (const char* name : {"d4", "e4", "d5", "e5"}) {
            const Piece& pc = p.at(*parseSquare(p, name));
            if (!pc.empty() && p.registry().get(pc.type).royal)
                return GameOutcome{pc.color == Color::White ? GameResult::WhiteWins : GameResult::BlackWins,
                                   "König im Zentrum"};
        }
        return std::nullopt;
    });
    // Nach jedem Zug verschwindet ein Bauer auf h7 (Stellungsmanipulation durch ein Plugin).
    v.hooks.onMovePlayed.add("chaos", 0, [](Position& p, const Move&) { p.clear(*parseSquare(p, "h7")); });
    v.startFen = "4k3/p6p/8/8/8/8/8/4K3 w - - 0 1";  // a7 bleibt, sonst waere es sofort Remis (Material)

    Game g(v);
    CHECK(g.play("Ke2"));
    CHECK(g.position().at(*parseSquare(g.position(), "h7")).empty());
    CHECK(g.play("Kd7"));
    CHECK(g.play("Ke3"));
    CHECK(g.play("Kd6"));
    CHECK(g.play("Ke4"));
    CHECK_EQ(g.outcome().reason, std::string("König im Zentrum"));
    CHECK(g.undo());
    CHECK(!g.outcome().over());
}

TEST_CASE(hook_priority_order) {
    HookList<void(std::vector<int>&)> list;
    list.add("a", 0, [](std::vector<int>& v) { v.push_back(1); });
    list.add("b", 5, [](std::vector<int>& v) { v.push_back(2); });
    list.add("c", 0, [](std::vector<int>& v) { v.push_back(3); });
    std::vector<int> order;
    for (const auto& e : list.entries()) e.fn(order);
    CHECK((order == std::vector<int>{2, 1, 3}));
    list.removeOwner("b");
    CHECK_EQ(list.entries().size(), size_t{2});
}
