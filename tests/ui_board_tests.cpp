// Prueft die Maus-Bedienung des Bretts ohne Fenster: ImGui laeuft "headless",
// Mausereignisse werden simuliert, und wir schauen, welcher Zug herauskommt.

#include "framework.hpp"

#include "core/fen.hpp"
#include "core/game.hpp"
#include "core/notation.hpp"
#include "ui/board_view.hpp"
#include "ui/settings.hpp"
#include "ui/theme.hpp"

#include <filesystem>

#include <imgui.h>

using namespace chess;
using namespace chess::ui;

namespace {

class Harness {
public:
    Harness() {
        ctx_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(900, 900);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();
        theme_.apply();
    }
    ~Harness() { ImGui::DestroyContext(ctx_); }

    // Ein Frame mit Maus an `mouse` und Taste `down`.
    std::optional<Move> frame(const Position& pos, ImVec2 mouse, bool down, bool flipped = false, int button = 0) {
        // ImGui kennt das Fenster unter der Maus erst ab dem zweiten Frame (wie in der echten App,
        // wo das Fenster laengst existiert, bevor jemand klickt).
        if (!warmed_) {
            warmed_ = true;
            frame(pos, ImVec2(1, 1), false, flipped);
            frame(pos, ImVec2(1, 1), false, flipped);
        }
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(button, down);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        const auto legal = pos.legalMoves();
        auto r = board_.draw(pos, legal, true, flipped, overlay, theme_, ImVec2(50, 50), ImVec2(800, 800));
        ImGui::End();
        ImGui::Render();
        return r;
    }

    // Mittelpunkt eines Feldes auf dem Bildschirm (Brett liegt bei 50..850, 100 px pro Feld).
    static ImVec2 center(const Position& pos, const char* name, bool flipped = false) {
        const Square s = *parseSquare(pos, name);
        const int col = flipped ? 7 - pos.fileOf(s) : pos.fileOf(s);
        const int row = flipped ? pos.rankOf(s) : 7 - pos.rankOf(s);
        return ImVec2(50 + col * 100 + 50.0f, 50 + row * 100 + 50.0f);
    }

    std::optional<Move> click(const Position& pos, const char* sq, bool flipped = false) {
        const ImVec2 p = center(pos, sq, flipped);
        std::optional<Move> r;
        if (auto m = frame(pos, p, false, flipped)) r = m;
        if (auto m = frame(pos, p, true, flipped)) r = m;
        if (auto m = frame(pos, p, false, flipped)) r = m;
        return r;
    }

    std::optional<Move> drag(const Position& pos, const char* from, const char* to) {
        const ImVec2 a = center(pos, from), b = center(pos, to);
        std::optional<Move> r;
        frame(pos, a, false);
        frame(pos, a, true);
        for (int i = 1; i <= 5; ++i)
            if (auto m = frame(pos, ImVec2(a.x + (b.x - a.x) * i / 5, a.y + (b.y - a.y) * i / 5), true)) r = m;
        if (auto m = frame(pos, b, false)) r = m;
        return r;
    }

    // Rechtsklick-Ziehen von `from` nach `to` (gleiches Feld = Kreis), optional mit Shift.
    void rightDrag(const Position& pos, const char* from, const char* to, bool shift = false) {
        const ImVec2 a = center(pos, from), b = center(pos, to);
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, shift);
        frame(pos, a, false, false, 1);
        frame(pos, a, true, false, 1);
        frame(pos, ImVec2((a.x + b.x) / 2, (a.y + b.y) / 2), true, false, 1);
        frame(pos, b, true, false, 1);
        frame(pos, b, false, false, 1);
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, false);
        frame(pos, b, false, false, 1);
    }

    BoardView& board() { return board_; }
    BoardOverlay overlay;

private:
    ImGuiContext* ctx_;
    bool warmed_ = false;
    Theme theme_;
    BoardView board_;
};

std::string san(const Position& pos, const std::optional<Move>& m) {
    if (!m) return "(kein Zug)";
    Position tmp = pos;
    return toSan(tmp, *m);
}

}  // namespace

TEST_CASE(ui_click_click_moves) {
    Harness h;
    const Position pos = fromFen(PieceRegistry::standard(), kStandardStartFen);
    CHECK(!h.click(pos, "e2").has_value());
    CHECK_EQ(san(pos, h.click(pos, "e4")), std::string("e4"));
}

TEST_CASE(ui_drag_and_drop_moves) {
    Harness h;
    const Position pos = fromFen(PieceRegistry::standard(), kStandardStartFen);
    CHECK_EQ(san(pos, h.drag(pos, "g1", "f3")), std::string("Nf3"));
}

TEST_CASE(ui_ignores_illegal_and_opponent_pieces) {
    Harness h;
    const Position pos = fromFen(PieceRegistry::standard(), kStandardStartFen);
    CHECK(!h.click(pos, "e7").has_value());  // schwarze Figur, Weiss am Zug
    CHECK(!h.click(pos, "e5").has_value());  // leeres Feld ohne Auswahl
    CHECK(!h.click(pos, "e2").has_value());
    CHECK(!h.click(pos, "e5").has_value());  // Bauer kann nicht 3 Felder
    CHECK(!h.drag(pos, "d1", "d4").has_value());  // Dame ist eingesperrt
}

TEST_CASE(ui_flipped_board_maps_squares) {
    Harness h;
    const Position pos = fromFen(PieceRegistry::standard(), "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    CHECK(!h.click(pos, "c7", true).has_value());
    CHECK_EQ(san(pos, h.click(pos, "c5", true)), std::string("c5"));
}

TEST_CASE(ui_promotion_waits_for_choice) {
    Harness h;
    const Position pos = fromFen(PieceRegistry::standard(), "8/4P3/8/8/8/8/k7/4K3 w - - 0 1");
    CHECK(!h.click(pos, "e7").has_value());
    // Vier moegliche Umwandlungen -> erst Auswahl-Popup, noch kein Zug
    CHECK(!h.click(pos, "e8").has_value());
}

TEST_CASE(ui_right_drag_draws_arrows_and_circles) {
    Harness h;
    const Position pos = fromFen(PieceRegistry::standard(), kStandardStartFen);
    const auto sq = [&](const char* n) { return *parseSquare(pos, n); };

    h.rightDrag(pos, "e2", "e4");
    CHECK_EQ(h.board().annotations().size(), size_t{1});
    CHECK(h.board().annotations()[0].from == sq("e2"));
    CHECK(h.board().annotations()[0].to == sq("e4"));
    CHECK_EQ(h.board().annotations()[0].color, 0);  // gruen

    h.rightDrag(pos, "d5", "d5", true);  // Kreis, mit Shift rot
    CHECK_EQ(h.board().annotations().size(), size_t{2});
    CHECK(h.board().annotations()[1].isCircle());
    CHECK_EQ(h.board().annotations()[1].color, 1);

    h.rightDrag(pos, "e2", "e4", true);  // gleicher Pfeil, andere Farbe -> umfaerben
    CHECK_EQ(h.board().annotations().size(), size_t{2});
    CHECK_EQ(h.board().annotations()[0].color, 1);
    h.rightDrag(pos, "e2", "e4", true);  // nochmal gleich -> entfernen
    CHECK_EQ(h.board().annotations().size(), size_t{1});

    // Pfeile sind keine Zuege
    CHECK(!h.click(pos, "a3").has_value());
    // Linksklick aufs Brett loescht alle Markierungen
    CHECK(h.board().annotations().empty());
}

TEST_CASE(ui_right_click_cancels_selection) {
    Harness h;
    const Position pos = fromFen(PieceRegistry::standard(), kStandardStartFen);
    CHECK(!h.click(pos, "e2").has_value());
    h.rightDrag(pos, "h5", "h5");
    CHECK(!h.click(pos, "e4").has_value());  // Auswahl war weg -> kein Zug
}

TEST_CASE(ui_annotations_can_be_disabled) {
    Harness h;
    h.overlay.allowAnnotations = false;
    const Position pos = fromFen(PieceRegistry::standard(), kStandardStartFen);
    h.rightDrag(pos, "e2", "e4");
    h.rightDrag(pos, "d5", "d5");
    CHECK(h.board().annotations().empty());
    CHECK_EQ(san(pos, h.drag(pos, "g1", "f3")), std::string("Nf3"));  // Ziehen geht weiterhin
}

TEST_CASE(settings_presets) {
    const auto none = AssistSettings::none();
    CHECK(!none.legalMoves && !none.hintButton && !none.undo && !none.botInfo && !none.evalBar);
    CHECK(none.lastMove);  // zeigt jedes echte Brett auch
    CHECK(AssistSettings::learning().hangingPieces);
    CHECK(!AssistSettings::standard().hangingPieces);
    CHECK(AssistSettings::standard().sameAssists(AssistSettings{}));
    CHECK(!AssistSettings::standard().sameAssists(AssistSettings::none()));

    // Design und Koordinaten zaehlen nicht zur Schnellwahl
    AssistSettings s = AssistSettings::none();
    s.darkTheme = false;
    s.coordinates = false;
    CHECK(s.sameAssists(AssistSettings::none()));
}

TEST_CASE(settings_save_and_load) {
    AssistSettings s = AssistSettings::none();
    s.annotations = false;
    s.darkTheme = false;
    const auto file = std::filesystem::temp_directory_path() / "chessengine_settings_test.json";
    CHECK(s.save(file));

    AssistSettings loaded;
    CHECK(loaded.load(file));
    CHECK(loaded.sameAssists(s));
    CHECK(!loaded.annotations);
    CHECK(!loaded.darkTheme);
    std::filesystem::remove(file);

    // Fehlende Felder behalten ihren Wert, Unsinn wird abgelehnt
    AssistSettings partial;
    CHECK(partial.fromJson(R"({"undo": false})"));
    CHECK(!partial.undo && partial.legalMoves);
    CHECK(!partial.fromJson("kein json"));
    CHECK(!AssistSettings{}.load(std::filesystem::temp_directory_path() / "gibt_es_nicht_123.json"));
}

TEST_CASE(ui_switch_selection_to_other_piece) {
    Harness h;
    const Position pos = fromFen(PieceRegistry::standard(), kStandardStartFen);
    CHECK(!h.click(pos, "e2").has_value());
    CHECK(!h.click(pos, "g1").has_value());  // Auswahl wechselt zum Springer
    CHECK_EQ(san(pos, h.click(pos, "h3")), std::string("Nh3"));
}
