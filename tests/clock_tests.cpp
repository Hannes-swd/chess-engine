#include "framework.hpp"

#include "core/clock.hpp"
#include "core/fen.hpp"
#include "core/game.hpp"

using namespace chess;

TEST_CASE(time_control_labels) {
    CHECK_EQ(TimeControl::minutes(1, 0).label(), std::string("1+0"));
    CHECK_EQ(TimeControl::minutes(3, 2).label(), std::string("3+2"));
    CHECK_EQ(TimeControl::minutes(0.5, 1).label(), std::string("0.5+1"));
    CHECK_EQ(TimeControl{}.label(), std::string("Ohne Uhr"));
    CHECK_EQ(TimeControl::minutes(15, 10).pgnTag(), std::string("900+10"));
    CHECK_EQ(TimeControl{}.pgnTag(), std::string("-"));
}

TEST_CASE(clock_consume_increment_flag) {
    Clock c(TimeControl::minutes(1, 2));
    CHECK_EQ(c.remaining(Color::White), 60000);
    c.consume(Color::White, 1500);
    CHECK_EQ(c.remaining(Color::White), 58500);
    CHECK_EQ(c.remaining(Color::Black), 60000);
    c.addIncrement(Color::White);
    CHECK_EQ(c.remaining(Color::White), 60500);
    c.consume(Color::Black, 999999);
    CHECK_EQ(c.remaining(Color::Black), 0);
    CHECK(c.flagged(Color::Black));
    CHECK(!c.flagged(Color::White));

    const auto snap = c.snapshot();
    c.consume(Color::White, 10000);
    c.restore(snap);
    CHECK_EQ(c.remaining(Color::White), 60500);

    Clock off;
    off.consume(Color::White, 5000);
    CHECK(!off.flagged(Color::White));  // ohne Uhr kann niemand die Zeit ueberschreiten
}

TEST_CASE(clock_format) {
    CHECK_EQ(Clock::format(300000), std::string("5:00"));
    CHECK_EQ(Clock::format(65000), std::string("1:05"));
    CHECK_EQ(Clock::format(9400), std::string("0:09.4"));
    CHECK_EQ(Clock::format(0), std::string("0:00.0"));
    CHECK_EQ(Clock::format(3723000), std::string("1:02:03"));
}

TEST_CASE(game_declared_result_and_undo) {
    Game g;
    CHECK(g.play("e4"));
    g.setResult({GameResult::BlackWins, "Zeit abgelaufen"});
    CHECK(g.outcome().result == GameResult::BlackWins);
    CHECK(!g.play("e5"));  // nach Zeitueberschreitung keine Zuege mehr
    CHECK(g.undo());
    CHECK(!g.outcome().over());  // Zuruecknehmen hebt das Ergebnis auf
}

TEST_CASE(mating_material) {
    const auto reg = PieceRegistry::standard();
    CHECK(!hasMatingMaterial(fromFen(reg, "4k3/8/8/8/8/8/8/4K3 w - - 0 1"), Color::White));
    CHECK(!hasMatingMaterial(fromFen(reg, "4k3/8/8/8/8/8/8/3NK3 w - - 0 1"), Color::White));
    CHECK(hasMatingMaterial(fromFen(reg, "4k3/8/8/8/8/8/8/2BNK3 w - - 0 1"), Color::White));
    CHECK(hasMatingMaterial(fromFen(reg, "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"), Color::White));
    CHECK(!hasMatingMaterial(fromFen(reg, "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"), Color::Black));
}
