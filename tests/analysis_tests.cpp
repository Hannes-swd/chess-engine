#include "framework.hpp"

#include "analysis/analysis.hpp"
#include "core/pgn.hpp"
#include "engine/builtin_engine.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace chess;

TEST_CASE(analysis_flags_fools_mate_blunder) {
    Game g;
    for (const char* m : {"f3", "e5", "g4", "Qh4"}) CHECK(g.play(m));

    BuiltinEngine e;
    SearchLimits lim;
    lim.depth = 3;
    int progressCalls = 0;
    const GameAnalysis a = analyseGame(g, e, lim, {}, [&](int, int total) {
        ++progressCalls;
        CHECK_EQ(total, 5);
    });
    CHECK(a.ok);
    CHECK_EQ(a.moves.size(), size_t{4});
    CHECK_EQ(progressCalls, 5);

    // 2. g4?? erlaubt Matt in 1
    CHECK(a.moves[2].quality == MoveQuality::Blunder);
    CHECK(a.moves[2].playedScore.mate && !a.moves[2].playedScore.winning);
    // 2... Qh4# ist der beste Zug
    CHECK(a.moves[3].quality == MoveQuality::Best);
    CHECK_EQ(a.moves[3].bestSan, std::string("Qh4#"));

    const auto comments = pgnComments(a);
    CHECK_EQ(comments.size(), size_t{4});
    CHECK(comments[2].find("[%eval #-1] Patzer") == 0);
    CHECK(comments[2].find("besser:") != std::string::npos);

    std::string pgn = toPgn(g, {}, &comments);
    std::replace(pgn.begin(), pgn.end(), '\n', ' ');  // PGN bricht nach ~80 Zeichen um
    CHECK(pgn.find("2. g4 {[%eval #-1] Patzer") != std::string::npos);
    CHECK(pgn.find("2... Qh4# {") != std::string::npos);

    if (pgn.find("2. g4 {[%eval #-1] Patzer") == std::string::npos) std::cerr << "  PGN war: " << pgn << "\n";

    const std::string text = formatAnalysis(g, a);
    CHECK(text.find("Patzer") != std::string::npos);
    CHECK(text.find("Weiß:") != std::string::npos);
}

TEST_CASE(analysis_formulas_and_lines) {
    CHECK(std::abs(winChance(Score::centipawns(0)) - 50.0) < 1e-9);
    CHECK(winChance(Score::centipawns(300)) > 70.0);
    CHECK_EQ(winChance(Score::mateIn(3, true)), 100.0);
    CHECK_EQ(winChance(Score::mateIn(1, false)), 0.0);
    CHECK(moveAccuracy(50, 50) > 99.9);
    CHECK(moveAccuracy(50, 0) < 15.0);
    CHECK(moveAccuracy(40, 60) > 99.9);  // Verbesserung ist kein Verlust

    Game g;
    for (const char* m : {"e4", "e5", "Nf3"}) CHECK(g.play(m));
    CHECK_EQ(lineText(g.startPosition(), g.moves()), std::string("1. e4 e5 2. Nf3"));
    CHECK_EQ(lineText(g.positionAt(1), {g.moves()[1], g.moves()[2]}), std::string("1... e5 2. Nf3"));
    CHECK_EQ(lineText(g.startPosition(), g.moves(), 2), std::string("1. e4 e5 …"));
}

TEST_CASE(analysis_explains_and_summarizes) {
    Game g;
    for (const char* m : {"f3", "e5", "g4", "Qh4"}) CHECK(g.play(m));
    BuiltinEngine e;
    SearchLimits lim;
    lim.depth = 3;
    const GameAnalysis a = analyseGame(g, e, lim);
    CHECK(a.ok);
    CHECK_EQ(a.whiteEvals.size(), size_t{5});
    const MoveReview& g4 = a.moves[2];
    CHECK_EQ(g4.headline, std::string("g4 ist ein Patzer"));
    CHECK(g4.explanation.find("mattsetzen") != std::string::npos);
    CHECK(g4.explanation.find("Besser war") != std::string::npos);
    CHECK(!g4.threatLineSan.empty() && g4.threatLineSan.front() == "Qh4#");
    CHECK(a.white.counts[static_cast<size_t>(MoveQuality::Blunder)] == 1);
    CHECK(a.black.accuracy > a.white.accuracy);
    CHECK(std::abs(a.whiteBetter + a.balanced + a.blackBetter - 100.0) < 0.01);
}

TEST_CASE(analysis_detects_missed_chance) {
    // 2. Ba6?? stellt den Laeufer ein, 2... Nf6 nimmt ihn nicht -> "Verpasst"
    Game g;
    for (const char* m : {"e4", "e5", "Ba6", "Nf6"}) CHECK(g.play(m));
    BuiltinEngine e;
    SearchLimits lim;
    lim.depth = 3;
    const GameAnalysis a = analyseGame(g, e, lim);
    CHECK(a.ok);
    CHECK(isError(a.moves[2].quality));
    CHECK(a.moves[3].quality == MoveQuality::Miss);
    CHECK(a.moves[3].explanation.find("Ba6") != std::string::npos);
    CHECK(a.moves[3].explanation.find("Besser war") != std::string::npos);
    if (a.moves[3].quality != MoveQuality::Miss)
        std::cerr << "  Nf6: " << qualityName(a.moves[3].quality) << " – " << a.moves[3].explanation << "\n";
}

TEST_CASE(analysis_marks_forced_moves) {
    // Schwarzer Koenig a8, weisser Koenig b6: a7 und b7 sind gedeckt, einziger Zug ist Kb8.
    Variant v = standardVariant();
    v.startFen = "k7/8/1K6/8/8/8/8/7R b - - 0 1";
    Game g(v);
    CHECK_EQ(g.legalMoves().size(), size_t{1});
    CHECK(g.play("Kb8"));
    BuiltinEngine e;
    SearchLimits lim;
    lim.depth = 2;
    const auto a = analyseGame(g, e, lim);
    CHECK(a.ok);
    CHECK(a.moves[0].quality == MoveQuality::Forced);
}

TEST_CASE(analysis_refuses_unsupported_engine) {
    class StandardOnly : public BuiltinEngine {
    public:
        bool supportsPosition(const Position&) const override { return false; }
    };
    Game g;
    CHECK(g.play("e4"));
    StandardOnly e;
    const auto a = analyseGame(g, e, SearchLimits{});
    CHECK(!a.ok);
    CHECK(!a.error.empty());
}
