#include "framework.hpp"

#include "core/fen.hpp"
#include "core/notation.hpp"
#include "engine/bot.hpp"
#include "engine/builtin_engine.hpp"

using namespace chess;

namespace {

// Liefert feste Kandidaten, damit sich der Stil-Mechanismus genau pruefen laesst.
class FakeEngine : public Engine {
public:
    std::vector<std::pair<std::string, int>> lines;  // SAN, Centipawns
    std::string best;
    int lastSkill = -1, lastElo = -1;

    std::string name() const override { return "Fake"; }
    bool supportsPosition(const Position&) const override { return true; }
    void configureStrength(int skill, int elo) override {
        lastSkill = skill;
        lastElo = elo;
    }
    SearchResult search(const Position& pos, const SearchLimits&) override {
        SearchResult r;
        r.ok = true;
        Position p = pos;
        for (const auto& [san, cp] : lines) {
            ScoredMove sm;
            sm.move = *parseMove(p, san);
            sm.score = Score::centipawns(cp);
            sm.pv = {sm.move};
            r.lines.push_back(sm);
        }
        r.bestMove = *parseMove(p, best.empty() ? lines.front().first : best);
        return r;
    }
};

// Weisser Bauer e4 kann ruhig nach e5 ziehen (30 cp) oder auf d5 schlagen (20 cp).
Variant duelVariant() {
    Variant v = standardVariant();
    v.startFen = "4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1";
    return v;
}

std::string botMove(BotProfile p, FakeEngine& e) {
    Game g(duelVariant());
    Bot bot(p, 42);
    const auto c = bot.choose(g, e);
    Position tmp = g.position();
    return toSan(tmp, c.move);
}

BotProfile styled(double aggression, int window) {
    BotProfile p = defaultBotProfile();
    p.aggression = aggression;
    p.window = window;
    p.randomness = 0;
    p.skill = 7;
    p.elo = 1600;
    return p;
}

}  // namespace

TEST_CASE(bot_style_aggressive_vs_defensive) {
    FakeEngine e;
    e.lines = {{"e5", 30}, {"exd5", 20}};
    CHECK_EQ(botMove(styled(1.0, 50), e), std::string("exd5"));
    CHECK_EQ(botMove(styled(-1.0, 50), e), std::string("e5"));
    CHECK_EQ(botMove(styled(0.0, 50), e), std::string("e5"));
    // Enges Fenster: der schlechtere Schlagzug kommt gar nicht in Frage
    CHECK_EQ(botMove(styled(1.0, 5), e), std::string("e5"));
    CHECK_EQ(e.lastSkill, 7);
    CHECK_EQ(e.lastElo, 1600);
}

TEST_CASE(bot_never_beats_engine_choice_when_strength_limited) {
    // Engine (z. B. Stockfish mit Skill Level) waehlt absichtlich den schwaecheren Zug exd5.
    FakeEngine e;
    e.lines = {{"e5", 30}, {"exd5", 20}};
    e.best = "exd5";
    CHECK_EQ(botMove(styled(-1.0, 50), e), std::string("exd5"));
}

TEST_CASE(bot_blunder_mode_avoids_engine_move) {
    FakeEngine e;
    e.lines = {{"e5", 30}, {"exd5", 20}};
    BotProfile p = styled(0, 50);
    p.blunderChance = 1.0;
    for (int i = 0; i < 5; ++i) CHECK(botMove(p, e) != "e5");
}

TEST_CASE(bot_with_builtin_engine_takes_mate) {
    Variant v = standardVariant();
    v.startFen = "6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1";
    Game g(v);
    BotProfile p = defaultBotProfile();
    p.depth = 3;
    p.movetimeMs = 0;
    p.aggression = -1;  // selbst ein vorsichtiger Bot nimmt das Matt
    BuiltinEngine e;
    Bot bot(p, 1);
    const auto c = bot.choose(g, e);
    CHECK(g.play(c.move));
    CHECK_EQ(g.sanMoves().back(), std::string("Ra8#"));
}

TEST_CASE(bot_respects_plugin_move_filter) {
    Variant v = standardVariant();
    v.hooks.filterMoves.add("nur-springer", 0, [](const Position& pos, std::vector<Move>& moves) {
        std::erase_if(moves, [&](const Move& m) { return pos.registry().get(pos.at(m.from).type).id != "knight"; });
    });
    Game g(v);
    BotProfile p = defaultBotProfile();
    p.depth = 2;
    p.movetimeMs = 0;
    BuiltinEngine e;
    Bot bot(p, 3);
    const auto c = bot.choose(g, e);
    CHECK_EQ(g.position().registry().get(g.position().at(c.move.from).type).id, std::string("knight"));
}

TEST_CASE(bot_tempo_instant_and_fixed) {
    Game g;
    BotProfile p = defaultBotProfile();
    p.tempo.mode = TempoMode::Instant;
    CHECK_EQ(Bot(p, 1).thinkDelayMs(g), 0);
    p.tempo.mode = TempoMode::Fixed;
    p.tempo.minMs = 1200;
    CHECK_EQ(Bot(p, 1).thinkDelayMs(g), 1200);
    // Fest, aber mit Uhr in Zeitnot: deutlich schneller
    BotContext ctx;
    ctx.remainingMs = 3000;
    CHECK(Bot(p, 1).thinkDelayMs(g, ctx) <= 250);
}

TEST_CASE(bot_tempo_human_varies_within_bounds) {
    // Mittelspiel-Stellung mit vielen Zuegen (keine Eroeffnungs-Abkuerzung)
    Variant v = standardVariant();
    v.startFen = "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R2QKB1R w KQ - 0 9";
    Game g(v);
    for (const char* m : {"Bd3", "dxc4", "Bxc4", "a6", "O-O", "b5", "Bd3", "Bb7", "a3"}) CHECK(g.play(m));

    BotProfile p = defaultBotProfile();
    p.tempo = {TempoMode::Human, 1000, 8000, 0.5};
    Bot bot(p, 42);
    int lo = 1 << 30, hi = 0;
    for (int i = 0; i < 200; ++i) {
        const int d = bot.thinkDelayMs(g);
        lo = std::min(lo, d);
        hi = std::max(hi, d);
        CHECK(d >= 400 && d <= 8000);  // nie unter 40 % von min, nie ueber max
    }
    CHECK(hi - lo > 2000);  // spuerbar unterschiedlich, nicht immer gleich
}

TEST_CASE(bot_tempo_human_is_quick_when_forced_or_short_on_time) {
    BotProfile p = defaultBotProfile();
    p.tempo = {TempoMode::Human, 2000, 10000, 0.5};

    // Nur ein legaler Zug: kaum Bedenkzeit
    Variant forced = standardVariant();
    forced.startFen = "k7/8/1K6/8/8/8/8/7R b - - 0 1";
    CHECK(Bot(p, 3).thinkDelayMs(Game(forced)) <= 800);

    // 4 Sekunden auf der Uhr: hoechstens ein Zwoelftel davon
    BotContext ctx;
    ctx.remainingMs = 4000;
    Bot bot(p, 5);
    for (int i = 0; i < 50; ++i) CHECK(bot.thinkDelayMs(Game(), ctx) <= 334);
}

TEST_CASE(bot_time_pressure_raises_blunders) {
    FakeEngine e;
    e.lines = {{"e5", 30}, {"exd5", 20}};
    BotProfile p = styled(0, 50);
    p.pressureBlunder = 1.0;  // bei fast leerer Uhr praktisch immer ein Fehler
    Game g(duelVariant());
    Bot bot(p, 7);
    BotContext ctx;
    ctx.remainingMs = 50;
    int blunders = 0;
    for (int i = 0; i < 20; ++i)
        if (bot.choose(g, e, ctx).reason == "Fehler in Zeitnot") ++blunders;
    CHECK(blunders >= 18);
    ctx.remainingMs = 60000;  // genug Zeit: keine Nervositaet
    for (int i = 0; i < 20; ++i) CHECK(bot.choose(g, e, ctx).reason != "Fehler in Zeitnot");
}

TEST_CASE(bot_profile_json_roundtrip) {
    BotProfile p = defaultBotProfile();
    p.name = "Test \"Bot\"";
    p.description = "Zeile 1\nZeile 2 – äöü";
    p.elo = 1800;
    p.aggression = -0.4;
    p.blunderChance = 0.05;
    p.pressureBlunder = 0.3;
    p.tempo = {TempoMode::Human, 700, 5500, 0.6};
    p.weights.capture = 55;

    std::string err;
    const auto parsed = json::parse(botProfileToJson(p), &err);
    CHECK(parsed.has_value());
    if (!parsed) {
        std::cerr << "  " << err << "\n";
        return;
    }
    const auto back = parseBotProfile(*parsed, err);
    CHECK(back.has_value());
    if (!back) return;
    CHECK_EQ(back->name, p.name);
    CHECK_EQ(back->description, p.description);
    CHECK_EQ(back->elo, 1800);
    CHECK_EQ(back->aggression, -0.4);
    CHECK_EQ(back->pressureBlunder, 0.3);
    CHECK(back->tempo.mode == TempoMode::Human);
    CHECK_EQ(back->tempo.minMs, 700);
    CHECK_EQ(back->tempo.maxMs, 5500);
    CHECK_EQ(back->weights.capture, 55.0);

    CHECK(!parseBotProfile(*json::parse(R"({"name":"X","tempo":{"mode":"langsam"}})"), err));
}

TEST_CASE(bot_profiles_load_from_folder) {
    std::vector<std::string> errors;
    const auto profiles = loadBotProfiles(CHESS_BOTS_DIR, errors);
    CHECK(errors.empty());
    CHECK(profiles.size() >= 5);
    bool foundAttacker = false;
    for (const auto& p : profiles)
        if (p.name == "Angreifer") {
            foundAttacker = true;
            CHECK(p.aggression > 0.5);
            CHECK_EQ(p.weights.capture, 70.0);
        }
    CHECK(foundAttacker);
    // Voreinstellungen: Lehrling denkt menschlich, Turbo zieht sofort
    for (const auto& p : profiles) {
        if (p.name == "Lehrling") CHECK(p.tempo.mode == TempoMode::Human && p.tempo.maxMs > p.tempo.minMs);
        if (p.name == "Turbo") CHECK(p.tempo.mode == TempoMode::Instant);
    }

    std::string err;
    CHECK(!parseBotProfile(*json::parse(R"({"description":"ohne Name"})"), err));
    CHECK(!parseBotProfile(*json::parse(R"({"name":"X","engine":"lc0"})"), err));
    const auto clamped = parseBotProfile(*json::parse(R"({"name":"X","aggression":5,"skill":-3})"), err);
    CHECK(clamped && clamped->aggression == 1.0 && clamped->skill == 0);
}
