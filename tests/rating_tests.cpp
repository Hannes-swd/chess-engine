#include "framework.hpp"

#include "engine/rating.hpp"

#include <cmath>

using namespace chess;

TEST_CASE(elo_expected_score) {
    CHECK(std::abs(expectedScore(1500, 1500) - 0.5) < 1e-9);
    CHECK(std::abs(expectedScore(1600, 1200) - 0.909) < 0.001);  // 400 Punkte Vorsprung ~ 91 %
    CHECK(std::abs(expectedScore(1200, 1600) - 0.091) < 0.001);
}

TEST_CASE(elo_delta_and_profile) {
    CHECK_EQ(eloDelta(1200, 1200, 1.0, 0), 20);   // K 40, halbe Erwartung
    CHECK_EQ(eloDelta(1200, 1200, 0.0, 0), -20);
    CHECK_EQ(eloDelta(1200, 1200, 0.5, 0), 0);
    CHECK_EQ(eloDelta(1200, 1200, 1.0, 50), 10);  // nach 30 Partien K 20
    CHECK(eloDelta(1200, 2000, 1.0, 0) > 35);     // Sieg gegen viel Staerkeren bringt viel
    CHECK(eloDelta(1200, 600, 1.0, 0) <= 2);      // Sieg gegen viel Schwaecheren kaum etwas

    PlayerProfile p;
    CHECK_EQ(applyResult(p, 1200, 1.0), 20);
    CHECK_EQ(p.rating, 1220);
    CHECK_EQ(p.peak, 1220);
    applyResult(p, 1220, 0.0);
    applyResult(p, 1200, 0.5);
    CHECK_EQ(p.games, 3);
    CHECK_EQ(p.wins, 1);
    CHECK_EQ(p.losses, 1);
    CHECK_EQ(p.draws, 1);
    CHECK_EQ(p.peak, 1220);
}

TEST_CASE(elo_bot_estimates) {
    BotProfile p = defaultBotProfile();
    p.skill = 20;
    CHECK_EQ(estimatedBotElo(p, true), 3000);
    p.elo = 1500;
    CHECK_EQ(estimatedBotElo(p, true), 1500);
    p.blunderChance = 0.2;  // Patzer machen ihn schwaecher
    CHECK(estimatedBotElo(p, true) < 1350);
    CHECK(estimatedBotElo(p, false) < 1000);  // ohne Stockfish deutlich schwaecher
    p.blunderChance = 1.0;
    p.randomness = 1000;
    CHECK_EQ(estimatedBotElo(p, false), 400);  // nie unter 400
}

TEST_CASE(player_profile_json) {
    PlayerProfile p;
    p.name = "Anna \"Turm\"";
    p.rating = 1337;
    p.games = 12;
    p.lastDelta = -8;
    PlayerProfile back;
    CHECK(playerProfileFromJson(playerProfileToJson(p), back));
    CHECK_EQ(back.name, p.name);
    CHECK_EQ(back.rating, 1337);
    CHECK_EQ(back.games, 12);
    CHECK_EQ(back.lastDelta, -8);
    CHECK(!playerProfileFromJson("kaputt", back));
}
