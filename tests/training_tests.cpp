// Eroeffnungsnamen, Aufgaben aus eigenen Fehlern, Wiederholung, Statistik aus PGN-Kommentaren.

#include "framework.hpp"

#include "analysis/analysis.hpp"
#include "core/fen.hpp"
#include "core/notation.hpp"
#include "core/openings.hpp"
#include "core/pgn.hpp"
#include "engine/builtin_engine.hpp"
#include "engine/rating.hpp"
#include "engine/training.hpp"

using namespace chess;

TEST_CASE(every_opening_line_is_legal) {
    CHECK(openings().size() >= 100u);
    for (const Opening& o : openings()) {
        Game g;
        for (const std::string& m : o.moves) {
            const bool ok = g.play(m);
            CHECK(ok);
            if (!ok) {
                std::cerr << "  " << o.name << ": " << m << " nicht legal\n";
                break;
            }
        }
    }
}

TEST_CASE(opening_names_are_recognised) {
    Game g;
    for (const char* m : {"e4", "e5", "Nf3", "Nc6", "Bc4", "Nf6", "Ng5", "d5", "exd5", "Nxd5", "Nxf7", "Kxf7"})
        CHECK(g.play(m));
    const OpeningMatch m = identifyOpening(g.sanMoves());
    CHECK(m.opening != nullptr);
    CHECK_EQ(m.opening->name, std::string("Zweispringerspiel, Fegatello-Angriff"));
    CHECK_EQ(m.plies, 11);

    // Schachzeichen stoeren nicht: 1. d4 Nf6 2. c4 e6 3. Nf3 Bb4+
    Game b;
    for (const char* mv : {"d4", "Nf6", "c4", "e6", "Nf3", "Bb4"}) CHECK(b.play(mv));
    CHECK_EQ(b.sanMoves().back(), std::string("Bb4+"));
    CHECK_EQ(identifyOpening(b.sanMoves()).opening->name, std::string("Bogoljubow-Indische Verteidigung"));
    CHECK(identifyOpening({"a3"}).opening == nullptr);
}

TEST_CASE(puzzles_from_own_mistakes_and_schedule) {
    // 1. f3? e5 2. g4?? Qh4# – Weiss patzt
    Game g;
    for (const char* m : {"f3", "e5", "g4", "Qh4"}) CHECK(g.play(m));
    BuiltinEngine engine;
    SearchLimits lim;
    lim.depth = 4;
    const GameAnalysis a = analyseGame(g, engine, lim);
    CHECK(a.ok);

    const auto puzzles = puzzlesFromAnalysis(g, a, Color::White, "Test", 1000);
    CHECK(!puzzles.empty());
    const Puzzle* g4 = nullptr;
    for (const auto& p : puzzles)
        if (p.playedSan == "g4") g4 = &p;
    CHECK(g4 != nullptr);
    if (g4) {
        CHECK_EQ(g4->theme, std::string("Matt zugelassen"));
        // Der gesuchte Zug ist in der Stellung legal
        Position pos = fromFen(PieceRegistry::standard(), g4->fen);
        CHECK(parseUciMove(pos, g4->bestUci).has_value());
    }
    // Schwarz hat keine Fehler gemacht
    CHECK(puzzlesFromAnalysis(g, a, Color::Black, "Test", 1000).empty());

    TrainingData data;
    CHECK_EQ(mergePuzzles(data, puzzles), static_cast<int>(puzzles.size()));
    CHECK_EQ(mergePuzzles(data, puzzles), 0);  // keine Doppelten
    CHECK_EQ(duePuzzles(data, 1000).size(), puzzles.size());

    Puzzle& p = data.puzzles.front();
    schedule(p, true, 1000);
    CHECK_EQ(p.box, 1);
    CHECK(p.due == 1000 + 24 * 3600);
    schedule(p, false, 2000);
    CHECK_EQ(p.box, 0);
    CHECK_EQ(p.failed, 1);

    // Speichern und Laden
    TrainingData back;
    data.openingRuns["Italienische Partie"] = 3;
    CHECK(trainingFromJson(trainingToJson(data), back));
    CHECK_EQ(back.puzzles.size(), data.puzzles.size());
    CHECK_EQ(back.puzzles.front().failed, 1);
    CHECK_EQ(back.puzzles.front().theme, data.puzzles.front().theme);
    CHECK_EQ(back.openingRuns["Italienische Partie"], 3);
}

TEST_CASE(stats_from_pgn_comments) {
    const auto rec = parsePgn(
        "[White \"Du\"]\n[Black \"Bot\"]\n[Result \"0-1\"]\n\n"
        "1. f3 {[%eval -0.60] Ungenau} 1... e5 {[%eval -0.40] Bester Zug} "
        "2. g4 {[%eval #-1] Patzer – besser: Nc3} 2... Qh4# {Bester Zug – Matt} 0-1");
    CHECK(rec.has_value());
    CHECK_EQ(rec->comments.size(), size_t{4});
    const auto white = qualityCountsFromComments(*rec, Color::White);
    CHECK_EQ(white[static_cast<size_t>(MoveQuality::Blunder)], 1);
    CHECK_EQ(white[static_cast<size_t>(MoveQuality::Inaccuracy)], 1);
    const auto acc = accuracyFromComments(*rec, Color::White);
    const auto accBlack = accuracyFromComments(*rec, Color::Black);
    CHECK(acc.has_value() && accBlack.has_value());
    if (acc && accBlack) CHECK(*acc < *accBlack);
    // Ohne Bewertungen keine Genauigkeit
    CHECK(!accuracyFromComments(*parsePgn("1. e4 e5 *"), Color::White).has_value());
}

TEST_CASE(rating_history_is_saved) {
    PlayerProfile p;
    applyResult(p, 1200, 1.0, "2026-11-26_1800", "Clubspieler");
    applyResult(p, 1200, 0.0, "2026-11-26_1900", "Clubspieler");
    CHECK_EQ(p.history.size(), size_t{3});  // Startpunkt + 2 Partien
    PlayerProfile back;
    CHECK(playerProfileFromJson(playerProfileToJson(p), back));
    CHECK_EQ(back.history.size(), size_t{3});
    CHECK_EQ(back.history.back().rating, p.rating);
    CHECK_EQ(back.history.back().opponent, std::string("Clubspieler"));
}
