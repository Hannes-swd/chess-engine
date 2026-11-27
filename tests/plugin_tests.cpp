// Lua-Plugins: Sandbox, Grenzen, Spiel-API und die mitgelieferten Beispiel-Plugins.

#include "framework.hpp"

#include "core/fen.hpp"
#include "core/notation.hpp"
#include "engine/builtin_engine.hpp"
#include "engine/engine.hpp"
#include "plugins/lua_plugin.hpp"

#include <algorithm>
#include <future>
#include <thread>
#include <filesystem>

using namespace chess;
using namespace chess::plugins;

namespace {

// Plugin aus Quelltext auf Standardschach anwenden
std::optional<Variant> variantFrom(const std::string& source, std::string* error = nullptr) {
    std::string err;
    auto p = LuaPlugin::fromSource(source, "test", &err);
    if (!p) {
        if (error) *error = err;
        return std::nullopt;
    }
    Variant v = standardVariant();
    auto reg = std::make_shared<PieceRegistry>(*PieceRegistry::standard());
    if (!p->install(v, *reg, 0, &err)) {
        if (error) *error = err;
        return std::nullopt;
    }
    v.pieces = reg;
    return v;
}

const PluginInfo* findPlugin(const std::vector<PluginInfo>& all, const std::string& id) {
    for (const auto& p : all)
        if (p.id == id) return &p;
    return nullptr;
}

}  // namespace

TEST_CASE(plugin_sandbox_blocks_system_access) {
    std::string err;
    // io/os gibt es nicht
    CHECK(!LuaPlugin::fromSource("io.open('x.txt', 'w') return {}", "io", &err));
    CHECK(err.find("io") != std::string::npos);
    CHECK(!LuaPlugin::fromSource("os.execute('calc') return {}", "os", &err));
    CHECK(!LuaPlugin::fromSource("require('socket') return {}", "req", &err));
    CHECK(!LuaPlugin::fromSource("load('return 1')() return {}", "load", &err));
    // Bytecode wird nicht geladen
    CHECK(!LuaPlugin::fromSource(std::string("\x1bLua", 4), "bytecode", &err));
    // Falscher Rueckgabewert
    CHECK(!LuaPlugin::fromSource("return 42", "noTable", &err));
    CHECK(err.find("Tabelle") != std::string::npos);
}

TEST_CASE(plugin_limits_stop_endless_loops_and_memory) {
    std::string err;
    CHECK(!LuaPlugin::fromSource("while true do end return {}", "loop", &err));
    CHECK(err.find("Rechenzeit") != std::string::npos);
    CHECK(!LuaPlugin::fromSource("local t = {} for i = 1, 1e9 do t[i] = ('x'):rep(1000) .. i end return {}", "mem",
                                 &err));
    // Endlosschleife in einem Hook: Fehler wird geloggt, das Spiel laeuft weiter
    auto v = variantFrom("return { filter_moves = function(b, m) while true do end end }");
    CHECK(v.has_value());
    Game g(*v);
    CHECK_EQ(g.legalMoves().size(), size_t{20});  // Hook gescheitert -> Zuege unveraendert
}

TEST_CASE(plugin_new_piece_and_board_size) {
    auto v = variantFrom(R"(
        return { on_load = function(rules)
            rules.add_piece{ id = "drache", symbol = "D", value = 800, moves_like = { "rook", "knight" } }
            rules.set_start("10/10/10/10/10/4D5/10/10/10/10 w - - 0 1")
        end })");
    CHECK(v.has_value());
    Game g(*v);
    CHECK_EQ(g.position().width(), 10);
    CHECK_EQ(g.legalMoves().size(), size_t{26});  // 18 Turmzuege + 8 Springerspruenge
}

TEST_CASE(plugin_hooks_filter_moves_and_end_game) {
    // Schlagzwang
    auto v = variantFrom(R"(
        return { filter_moves = function(board, moves)
            local c = {}
            for _, m in ipairs(moves) do if m.capture then c[#c + 1] = m end end
            if #c > 0 then return c end
            return moves
        end })");
    CHECK(v.has_value());
    Game g(*v);
    for (const char* m : {"e4", "d5"}) CHECK(g.play(m));
    CHECK_EQ(g.legalMoves().size(), size_t{1});  // nur exd5
    CHECK(g.play("exd5"));

    // Eigene Siegbedingung + Brett lesen
    auto hill = variantFrom(R"(
        return { check_end = function(board, info)
            local id, color = board:get(5, 4)
            if id == "pawn" and color == "white" then return "white", "Bauer auf e4" end
        end })");
    CHECK(hill.has_value());
    Game h(*hill);
    CHECK(!h.outcome().over());
    CHECK(h.play("e4"));
    CHECK(h.outcome().result == GameResult::WhiteWins);
    CHECK_EQ(h.outcome().reason, std::string("Bauer auf e4"));
}

TEST_CASE(plugin_on_move_edits_board_and_errors_are_contained) {
    // Nach jedem Zug verschwindet der Bauer auf a2 (Brett darf veraendert werden)
    auto v = variantFrom(R"(
        return { on_move = function(board, move) board:clear(1, 2) end })");
    CHECK(v.has_value());
    Game g(*v);
    CHECK(g.play("e4"));
    CHECK(g.position().at(g.position().square(0, 1)).empty());

    // Lesendes Brett darf nicht veraendert werden; Fehler landet im Log, nicht im Absturz
    std::string err;
    auto p = LuaPlugin::fromSource("return { filter_moves = function(b, m) b:clear(1, 1) return m end }", "ro", &err);
    CHECK(p != nullptr);
    Variant rv = standardVariant();
    auto reg = std::make_shared<PieceRegistry>(*PieceRegistry::standard());
    CHECK(p->install(rv, *reg, 0, &err));
    rv.pieces = reg;
    Game r(rv);
    CHECK_EQ(r.legalMoves().size(), size_t{20});
    CHECK(p->errorCount() > 0);
    const auto log = p->log();
    CHECK(!log.empty() && log.back().find("gelesen") != std::string::npos);
}

TEST_CASE(bundled_plugins_load_and_play) {
    const auto all = discover({CHESS_PLUGINS_DIR});
    CHECK(all.size() >= 9u);
    for (const PluginInfo& info : all) {
        const VariantBuild b = buildVariant({info});
        CHECK(b.errors.empty());
        if (!b.errors.empty()) std::cerr << "  " << b.errors.front() << "\n";
        Game g(b.variant);
        CHECK(!g.legalMoves().empty());
        // jedes Plugin aendert die Regeln – ausser den beiden, die nur eine andere Startstellung waehlen
        CHECK(!usesStandardRules(g) || info.id == "zufallsaufstellung" || info.id == "zufallsmittelspiel");
    }

    // Grashuepfer: Lua-Zugfunktion, auch fuer die eingebaute Engine nutzbar
    const PluginInfo* gh = findPlugin(all, "grashuepfer");
    CHECK(gh != nullptr);
    if (gh) {
        Game g(buildVariant({*gh}).variant);
        bool hopsToC7 = false;
        for (const Move& m : g.legalMoves())
            if (squareName(g.position(), m.from) == "c3" && squareName(g.position(), m.to) == "c7") hopsToC7 = true;
        // c3 -> Linie nach oben: c4..c5 leer, c6 = schwarzer Grashuepfer, Landung c7 (schwarzer Bauer) = Schlag
        CHECK(hopsToC7);
    }

    // Riesenbrett + Koenigshuegel zusammen
    const PluginInfo* big = findPlugin(all, "riesenbrett");
    const PluginInfo* hill = findPlugin(all, "koenigshuegel");
    CHECK(big && hill);
    if (big && hill) {
        const VariantBuild b = buildVariant({*hill, *big});
        CHECK(b.errors.empty());
        CHECK_EQ(b.loaded.size(), size_t{2});
        Game g(b.variant);
        CHECK_EQ(g.position().width(), 10);
    }

    // Atomschach: Schlag laesst Nachbarn explodieren
    const PluginInfo* atom = findPlugin(all, "atomschach");
    CHECK(atom != nullptr);
    if (atom) {
        Game g(buildVariant({*atom}).variant);
        for (const char* m : {"e4", "d5", "exd5"}) CHECK(g.play(m));
        // Auf d5 explodiert: weisser Bauer weg, schwarze Dame d8 bleibt (2 Felder entfernt)
        CHECK(g.position().at(g.position().square(3, 4)).empty());
    }
}

TEST_CASE(broken_plugin_falls_back_to_standard) {
    PluginInfo info;
    info.name = "kaputt";
    info.dir = std::filesystem::path(CHESS_PLUGINS_DIR) / "gibt_es_nicht";
    const VariantBuild b = buildVariant({info});
    CHECK(!b.errors.empty());
    Game g(b.variant);
    CHECK(usesStandardRules(g));
}

TEST_CASE(random_middlegame_plugin_starts_mid_game) {
    const auto all = discover({CHESS_PLUGINS_DIR});
    const PluginInfo* mid = findPlugin(all, "zufallsmittelspiel");
    CHECK(mid != nullptr);
    if (!mid) return;
    // Mehrmals bauen: jede Stellung muss gueltig sein, nicht im Schach, mitten in der Partie
    for (int i = 0; i < 30; ++i) {
        const VariantBuild b = buildVariant({*mid});
        CHECK(b.errors.empty());
        Game g(b.variant);
        CHECK(g.startPosition().fullmoveNumber() >= 10);
        CHECK(!g.position().inCheck(g.position().sideToMove()));
        CHECK(g.legalMoves().size() > 5u);
        CHECK(b.variant.name.rfind("Mittelspiel aus: ", 0) == 0);
        CHECK(usesStandardRules(g));  // Stockfish darf spielen und analysieren
    }
}

TEST_CASE(mate_in_one_plugin_has_exactly_one_solution) {
    const auto all = discover({CHESS_PLUGINS_DIR});
    const PluginInfo* mate = findPlugin(all, "matt_in_eins");
    CHECK(mate != nullptr);
    if (!mate) return;
    CHECK(mate->playerStarts);
    int black = 0;
    for (int i = 0; i < 40; ++i) {
        const Variant v = buildVariant({*mate}).variant;
        Game g(v);
        const Color mover = g.position().sideToMove();
        if (mover == Color::Black) ++black;
        CHECK(!g.outcome().over());
        // genau ein Mattzug
        std::vector<Move> mates, others;
        for (const Move& m : g.legalMoves()) {
            Position p = g.position();
            p.makeMove(m);
            (p.inCheck(p.sideToMove()) && p.legalMoves().empty() ? mates : others).push_back(m);
        }
        CHECK_EQ(mates.size(), size_t{1});
        if (mates.size() != 1 || others.empty()) continue;
        // Matt gefunden -> Weiss gewinnt
        Game win(v);
        CHECK(win.play(mates.front()));
        CHECK(win.outcome().result == (mover == Color::White ? GameResult::WhiteWins : GameResult::BlackWins));
        CHECK(win.outcome().reason.rfind("Matt gefunden", 0) == 0);
        // anderer Zug -> Aufgabe vorbei, Loesung im Grund
        Game lose(v);
        CHECK(lose.play(others.front()));
        CHECK(lose.outcome().result == (mover == Color::White ? GameResult::BlackWins : GameResult::WhiteWins));
        CHECK(lose.outcome().reason.find("gesucht war") != std::string::npos);
        // Die genannte Loesung ist wirklich der Mattzug (auch gespiegelt)
        Position tmp = g.position();
        CHECK(win.outcome().reason.find(toSan(tmp, mates.front())) != std::string::npos);
    }
    CHECK(black > 5 && black < 35);  // etwa die Haelfte gespiegelt
}

TEST_CASE(higher_priority_plugin_wins_conflicts) {
    const auto all = discover({CHESS_PLUGINS_DIR});
    const PluginInfo* big = findPlugin(all, "riesenbrett");
    const PluginInfo* mid = findPlugin(all, "zufallsmittelspiel");
    CHECK(big && mid);
    if (!big || !mid) return;
    // Beide wollen die Startstellung festlegen: das Plugin mit der hoeheren Prioritaet setzt sich durch
    PluginInfo a = *big, b = *mid;
    a.priority = 990, b.priority = 980;
    Game g1(buildVariant({b, a}).variant);
    CHECK_EQ(g1.position().width(), 10);
    a.priority = 980, b.priority = 990;
    Game g2(buildVariant({a, b}).variant);
    CHECK_EQ(g2.position().width(), 8);
    CHECK(g2.startPosition().fullmoveNumber() >= 10);
}

TEST_CASE(plugin_piece_images_stay_inside_plugin_folder) {
    const auto all = discover({CHESS_PLUGINS_DIR});
    const PluginInfo* big = findPlugin(all, "riesenbrett");
    CHECK(big != nullptr);
    if (big) {
        const Variant v = buildVariant({*big}).variant;
        const auto id = v.pieces->findById("drache");
        CHECK(id.has_value());
        if (id) {
            const PieceType& t = v.pieces->get(*id);
            CHECK(t.imageWhite.find("drache_weiss.png") != std::string::npos);
            CHECK(std::filesystem::exists(t.imageWhite));
            CHECK(std::filesystem::exists(t.imageBlack));
        }
    }
    // Pfade aus dem Plugin-Ordner heraus werden abgelehnt
    std::string err;
    auto p = LuaPlugin::fromSource(
        "return { on_load = function(r) r.add_piece{ id = 'x', symbol = 'X', image = '../../geheim.png' } end }", "img",
        &err);
    CHECK(p != nullptr);
    Variant v = standardVariant();
    auto reg = std::make_shared<PieceRegistry>(*PieceRegistry::standard());
    CHECK(!p->install(v, *reg, 0, &err));
    CHECK(err.find("Plugin-Ordner") != std::string::npos);
}

TEST_CASE(builtin_engine_understands_plugin_rules) {
    // Koenigshuegel: ein Koenigszug auf d4/e4 gewinnt sofort – das sieht die Engine nur mit den Plugin-Regeln
    auto v = variantFrom(R"(
        return {
          on_load = function(rules) rules.set_start("7k/6pp/8/8/8/4K3/PP6/8 w - - 0 1") end,
          check_end = function(board)
            for _, sq in ipairs({ {4,4}, {5,4}, {4,5}, {5,5} }) do
              local id, color = board:get(sq[1], sq[2])
              if id == "king" then return color, "Huegel" end
            end
          end,
        })");
    CHECK(v.has_value());
    if (!v) return;
    Game g(*v);
    BuiltinEngine engine;
    SearchLimits lim;
    lim.depth = 2;
    lim.game = &g;
    const SearchResult r = engine.search(g.position(), lim);
    CHECK(r.ok && r.bestMove.valid());
    const std::string to = squareName(g.position(), r.bestMove.to);
    CHECK(!r.lines.empty() && r.lines.front().score.mate && r.lines.front().score.winning);

    // Atomschach: Schlagen neben dem eigenen Koenig sprengt ihn – die Engine darf das nicht tun.
    // Weiss: Kc1, Turm a8; Schwarz: Kh8, Laeufer b2 (neben Kc1). Rxb2?? gibt es nicht, aber Kxb2 waere
    // im Atomschach Selbstmord (der Koenig explodiert mit).
    const auto all = discover({CHESS_PLUGINS_DIR});
    const PluginInfo* atom = findPlugin(all, "atomschach");
    CHECK(atom != nullptr);
    if (atom) {
        Variant av = buildVariant({*atom}).variant;
        av.startFen = "7k/8/8/8/8/8/1b6/2K4R w - - 0 1";
        Game ag(av);
        SearchLimits al;
        al.depth = 2;
        al.game = &ag;
        const SearchResult ar = engine.search(ag.position(), al);
        CHECK(ar.bestMove.valid());
        CHECK(squareName(ag.position(), ar.bestMove.to) != "b2");  // nicht mit dem Koenig schlagen
        // Ohne Plugin-Regeln wuerde die Engine den freien Laeufer einfach nehmen
        SearchLimits plain;
        plain.depth = 2;
        const SearchResult pr = engine.search(ag.position(), plain);
        CHECK(squareName(ag.position(), pr.bestMove.to) == "b2");
    }
}

TEST_CASE(plugin_ui_events_only_on_ui_thread) {
    setUiThread(std::this_thread::get_id());
    const auto all = discover({CHESS_PLUGINS_DIR});
    const PluginInfo* atom = findPlugin(all, "atomschach");
    CHECK(atom != nullptr);
    if (!atom) return;
    VariantBuild b = buildVariant({*atom});
    CHECK_EQ(b.loaded.size(), size_t{1});
    for (auto& lp : b.loaded) lp->takeUiEvents();
    Game g(b.variant);
    for (const char* m : {"e4", "d5", "exd5"}) CHECK(g.play(m));
    const auto events = b.loaded.front()->takeUiEvents();
    bool message = false, mark = false;
    for (const auto& e : events) {
        message |= e.kind == UiEvent::Kind::Message && e.text.find("Explosion") != std::string::npos;
        mark |= e.kind == UiEvent::Kind::Mark && e.x1 == 4 && e.y1 == 5 && e.color == 1;
    }
    CHECK(message);
    CHECK(mark);
    // Gleicher Zug in einem anderen Thread (wie eine Engine-Suche): keine Meldungen
    Game g2(b.variant);
    std::async(std::launch::async, [&] {
        for (const char* m : {"e4", "d5", "exd5"}) g2.play(m);
    }).wait();
    CHECK(b.loaded.front()->takeUiEvents().empty());
    setUiThread({});
}

TEST_CASE(plugins_speak_english_when_asked) {
    const auto all = discover({CHESS_PLUGINS_DIR});
    const PluginInfo* big = findPlugin(all, "riesenbrett");
    const PluginInfo* mate = findPlugin(all, "matt_in_eins");
    const PluginInfo* mid = findPlugin(all, "zufallsmittelspiel");
    CHECK(big && mate && mid);
    if (!big || !mate || !mid) return;
    CHECK_EQ(big->displayName(true), std::string("Giant Board 10×10"));
    CHECK(!big->displayDescription(true).empty());
    setLanguage(true);
    const Variant bv = buildVariant({*big}).variant;
    CHECK_EQ(bv.name, std::string("Giant Board 10×10"));
    CHECK_EQ(bv.pieces->get(*bv.pieces->findById("drache")).name, std::string("Dragon"));
    const Variant mv = buildVariant({*mid}).variant;
    CHECK(mv.name.rfind("Middlegame from: ", 0) == 0);
    CHECK(mv.name.find("Partie") == std::string::npos);  // englischer Eroeffnungsname
    const Variant tv = buildVariant({*mate}).variant;
    Game g(tv);
    std::vector<Move> others;
    for (const Move& m : g.legalMoves()) {
        Position p = g.position();
        p.makeMove(m);
        if (!(p.inCheck(p.sideToMove()) && p.legalMoves().empty())) others.push_back(m);
    }
    CHECK(!others.empty());
    if (!others.empty()) {
        CHECK(g.play(others.front()));
        CHECK(g.outcome().reason.rfind("No mate", 0) == 0);
    }
    setLanguage(false);
    CHECK_EQ(buildVariant({*big}).variant.name, std::string("Riesenbrett 10×10"));
}