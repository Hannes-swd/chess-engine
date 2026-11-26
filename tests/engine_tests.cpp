#include "framework.hpp"

#include "core/fen.hpp"
#include "core/game.hpp"
#include "core/notation.hpp"
#include "engine/builtin_engine.hpp"

using namespace chess;

namespace {

std::string bestSan(const char* fen, SearchLimits lim, Score* score = nullptr) {
    BuiltinEngine e;
    Position pos = fromFen(PieceRegistry::standard(), fen);
    const SearchResult r = e.search(pos, lim);
    CHECK(r.ok);
    CHECK(r.bestMove.valid());
    if (score && !r.lines.empty()) *score = r.lines.front().score;
    return r.bestMove.valid() ? toSan(pos, r.bestMove) : "";
}

}  // namespace

TEST_CASE(score_formatting_and_order) {
    CHECK_EQ(Score::centipawns(35).toString(), std::string("+0.35"));
    CHECK_EQ(Score::centipawns(-120).toString(), std::string("-1.20"));
    CHECK_EQ(Score::centipawns(-5).toPgnEval(), std::string("-0.05"));
    CHECK_EQ(Score::mateIn(3, true).toString(), std::string("#3"));
    CHECK_EQ(Score::mateIn(2, false).toString(), std::string("#-2"));
    CHECK(Score::mateIn(5, true).order() > Score::centipawns(5000).order());
    CHECK(Score::mateIn(1, true).order() > Score::mateIn(3, true).order());
    CHECK(Score::mateIn(0, false).order() < Score::mateIn(4, false).order());
    CHECK(Score::mateIn(2, true).negated().winning == false);
    CHECK_EQ(Score::centipawns(40).negated().value, -40);
}

TEST_CASE(builtin_finds_mate_in_one) {
    SearchLimits lim;
    lim.depth = 3;
    Score s;
    CHECK_EQ(bestSan("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", lim, &s), std::string("Ra8#"));
    CHECK(s.mate && s.winning && s.value == 1);
}

TEST_CASE(builtin_wins_free_queen) {
    SearchLimits lim;
    lim.depth = 3;
    CHECK_EQ(bestSan("4k3/8/8/3q4/8/8/3R4/4K3 w - - 0 1", lim), std::string("Rxd5"));
}

TEST_CASE(builtin_handles_mated_position) {
    // Schwarz ist bereits matt: die Suche meldet ok, aber keinen Zug.
    BuiltinEngine e;
    Position mated = fromFen(PieceRegistry::standard(), "R5k1/5ppp/8/8/8/8/8/6K1 b - - 1 1");
    const SearchResult r = e.search(mated, SearchLimits{});
    CHECK(r.ok);
    CHECK(!r.bestMove.valid());
    CHECK(r.lines.empty());
}

TEST_CASE(builtin_multipv_and_time_limit) {
    BuiltinEngine e;
    Position pos = fromFen(PieceRegistry::standard(), kStandardStartFen);
    SearchLimits lim;
    lim.movetimeMs = 200;
    lim.multiPv = 4;
    const auto t0 = std::chrono::steady_clock::now();
    const SearchResult r = e.search(pos, lim);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(r.ok);
    CHECK_EQ(r.lines.size(), size_t{4});
    CHECK(ms < 1000);
    for (size_t i = 1; i < r.lines.size(); ++i) CHECK(r.lines[i - 1].score.order() >= r.lines[i].score.order());
}

TEST_CASE(builtin_respects_root_moves) {
    BuiltinEngine e;
    Position pos = fromFen(PieceRegistry::standard(), kStandardStartFen);
    SearchLimits lim;
    lim.depth = 2;
    lim.rootMoves = {*parseMove(pos, "a3"), *parseMove(pos, "h3")};
    const SearchResult r = e.search(pos, lim);
    const std::string san = toSan(pos, r.bestMove);
    CHECK(san == "a3" || san == "h3");
}

TEST_CASE(builtin_plays_variants) {
    // Drache (Turm + Springer) auf 10x10: die eingebaute Engine kommt damit klar.
    auto reg = PieceRegistry::standard();
    PieceType d;
    d.id = "drache";
    d.name = "Drache";
    d.symbol = 'D';
    d.value = 800;
    d.steps = steps::combine(steps::orthogonal(true), steps::knight());
    reg->add(d);
    Position pos = fromFen(reg, "4k5/10/10/10/10/10/10/10/10/4K4D w - - 0 1");
    BuiltinEngine e;
    SearchLimits lim;
    lim.depth = 2;
    const SearchResult r = e.search(pos, lim);
    CHECK(r.ok);
    CHECK(r.bestMove.valid());
    CHECK(e.evaluate(pos) > 700);  // Weiss hat einen Drachen mehr
}

TEST_CASE(standard_rules_detection) {
    CHECK(usesStandardRules(Game()));
    Variant v = standardVariant();
    v.hooks.filterMoves.add("x", 0, [](const Position&, std::vector<Move>&) {});
    CHECK(!usesStandardRules(Game(v)));
}
