// Rahmen der Oberflaeche: Top-Leiste, linke Navigation, rechtes Panel.
// Moeglichst Icons (Google Material Icons) statt Text; jede Schaltflaeche hat einen Tooltip.

#include "ui/gui_app.hpp"

#include "core/notation.hpp"
#include "core/openings.hpp"
#include "core/pgn.hpp"
#include "ui/gui_internal.hpp"
#include "ui/icons.hpp"

#include <algorithm>
#include <string>

namespace chess::ui {

using detail::kTimePresetCount;
using detail::kTimePresets;
using detail::TimePreset;

namespace {

ImVec4 alpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

std::string withIcon(const Icon& i, const std::string& text) { return std::string(i) + "  " + text; }

constexpr float kNavItemHeight = 48.0f;  // Hoehe eines Eintrags in der linken Navigation (unskaliert)

// Einfaches Minus: Bahnschrift kennt das typografische Minus (U+2212) nicht
std::string signedInt(int v) { return (v > 0 ? "+" : v < 0 ? "-" : "±") + std::to_string(std::abs(v)); }

}  // namespace

// ===========================================================================
// Bausteine
// ===========================================================================

bool GuiApp::iconButton(const char* id, const char* glyph, const char* tooltip, bool active, float width,
                        bool enabled) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const float h = 22.0f * s + 2 * ImGui::GetStyle().FramePadding.y;
    const ImVec2 size(width > 0 ? width : h, h);
    // Textfarbe vor dem Push lesen: Aufrufer duerfen sie mit ImGuiCol_Text einfaerben
    const ImVec4 fg = !enabled ? alpha(c.textMuted, 0.45f) : active ? c.accent : ImGui::GetStyleColorVec4(ImGuiCol_Text);

    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button, active ? alpha(c.accent, 0.22f) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(c.accent, active ? 0.30f : 0.16f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, alpha(c.accent, 0.35f));
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Button("##icon", size);
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    // Icon exakt mittig (am Glyphen gemessen, nicht an der Zeilenhoehe)
    Theme::glyphCentered(ImGui::GetWindowDrawList(), theme_.body ? theme_.body : ImGui::GetFont(), 22.0f * s,
                         ImVec2(p0.x + size.x * 0.5f, p0.y + size.y * 0.5f), Theme::u32(fg), glyph);
    if (tooltip && *tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return clicked;
}

bool GuiApp::navItem(const char* glyph, const char* label, bool active, float width) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const float h = kNavItemHeight * s;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##nav", ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + width, p0.y + h);
    if (active || hovered)
        dl->AddRectFilled(ImVec2(p0.x + 8 * s, p0.y + 3 * s), ImVec2(p1.x - 8 * s, p1.y - 3 * s),
                          Theme::u32(active ? alpha(c.accent, 0.16f) : c.surfaceRaised), 8 * s);
    if (active)
        dl->AddRectFilled(ImVec2(p0.x + 2 * s, p0.y + 12 * s), ImVec2(p0.x + 5 * s, p1.y - 12 * s),
                          Theme::u32(c.accent), 2 * s);

    const bool expanded = settings_.sidebarExpanded;
    const float midY = p0.y + h * 0.5f;
    const float iconX = expanded ? p0.x + 34 * s : p0.x + width * 0.5f;
    Theme::glyphCentered(dl, theme_.body, 24.0f * s, ImVec2(iconX, midY), Theme::u32(active ? c.accent : c.textMuted),
                         glyph);
    if (expanded) {
        ImFont* f = theme_.strong ? theme_.strong : ImGui::GetFont();
        const float fs = 17.0f * s;
        const ImVec2 lsz = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
        dl->AddText(f, fs, ImVec2(p0.x + 60 * s, midY - lsz.y * 0.5f), Theme::u32(active ? c.text : c.textMuted), label);
    } else if (hovered) {
        ImGui::SetTooltip("%s", label);
    }
    return clicked;
}

bool GuiApp::iconTab(const char* glyph, const char* label, bool active, float width) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const float h = 60.0f * s;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##tab", ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered && !active) dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + h), Theme::u32(c.surfaceRaised), 8 * s);
    const ImU32 col = Theme::u32(active ? c.accent : c.textMuted);
    Theme::glyphCentered(dl, theme_.body, 24.0f * s, ImVec2(p0.x + width * 0.5f, p0.y + 20 * s), col, glyph);
    ImFont* f = theme_.strong ? theme_.strong : ImGui::GetFont();
    const float fs = 15.0f * s;
    const ImVec2 lsz = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
    dl->AddText(f, fs, ImVec2(p0.x + (width - lsz.x) * 0.5f, p0.y + 36 * s), Theme::u32(active ? c.text : c.textMuted),
                label);
    if (active)
        dl->AddRectFilled(ImVec2(p0.x + width * 0.3f, p0.y + h - 3 * s), ImVec2(p0.x + width * 0.7f, p0.y + h),
                          Theme::u32(c.accent), 2 * s);
    return clicked;
}

void GuiApp::panelHeader(const char* glyph, const char* title) {
    const float s = theme_.scale;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float h = 30.0f * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Theme::glyphCentered(dl, theme_.body, 24.0f * s, ImVec2(p0.x + 12 * s, p0.y + h * 0.5f), Theme::u32(theme_.p().accent),
                         glyph);
    ImFont* f = theme_.heading ? theme_.heading : ImGui::GetFont();
    const float fs = 24.0f * s;
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, title);
    dl->AddText(f, fs, ImVec2(p0.x + 34 * s, p0.y + (h - ts.y) * 0.5f), Theme::u32(theme_.p().text), title);
    ImGui::Dummy(ImVec2(0, h + 6 * s));
}

// ===========================================================================
// Top-Leiste: Logo, Status, Wertung | Elo, Bilanz, Engine, Design, Zen
// ===========================================================================

void GuiApp::drawTopBar(float height) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float W = ImGui::GetWindowWidth();
    const float midY = wp.y + height * 0.5f;
    dl->AddLine(ImVec2(wp.x, wp.y + height - 1), ImVec2(wp.x + W, wp.y + height - 1), Theme::u32(c.border));

    const float btn = 22.0f * s + 2 * ImGui::GetStyle().FramePadding.y;  // Hoehe der Icon-Knoepfe
    const float btnY = (height - btn) * 0.5f;
    ImFont* f = theme_.body ? theme_.body : ImGui::GetFont();
    ImFont* sf = theme_.strong ? theme_.strong : f;
    ImFont* hf = theme_.heading ? theme_.heading : sf;
    const float fs = 17.0f * s;
    auto textAt = [&](float x, ImFont* font, float size, ImU32 col, const char* t) {
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t);
        dl->AddText(font, size, ImVec2(wp.x + x, midY - sz.y * 0.5f), col, t);
        return sz.x;
    };
    auto iconAt = [&](float x, float size, ImU32 col, const char* glyph) {
        Theme::glyphCentered(dl, f, size, ImVec2(wp.x + x + size * 0.5f, midY), col, glyph);
        return size;
    };

    // --- links: Menue-Knopf und Logo -------------------------------------------
    float x = 14 * s;
    ImGui::SetCursorPos(ImVec2(x, btnY));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (iconButton("##navtoggle", settings_.sidebarExpanded ? icon::menuOpen : icon::menu,
                   settings_.sidebarExpanded ? "Seitenleiste einklappen" : "Seitenleiste ausklappen")) {
        settings_.sidebarExpanded = !settings_.sidebarExpanded;
        saveSettings();
    }
    x += btn + 14 * s;
    if (theme_.pieces) {
        Theme::glyphCentered(dl, theme_.pieces, 30 * s, ImVec2(wp.x + x + 12 * s, midY), Theme::u32(c.accent),
                             "\xE2\x99\x9E");
        x += 30 * s;
    }
    x += textAt(x, hf, 23 * s, Theme::u32(c.text), "ChessEngine") + 40 * s;

    // --- Mitte: Status der Partie ----------------------------------------------
    const GameOutcome out = game_.outcome();
    std::string state;
    const Icon* stateIcon = nullptr;
    ImU32 stateCol = Theme::u32(c.text);
    if (out.over()) {
        state = resultTitle(out) + ", " + out.reason;
        stateIcon = &icon::trophy;
        stateCol = Theme::u32(c.accent);
    } else if (botThinking()) {
        state = bot_->profile().name + " denkt nach";
        stateIcon = &icon::psychology;
    } else if (!isLive()) {
        state = "Zug " + std::to_string(viewPly()) + " von " + std::to_string(game_.ply());
        stateIcon = &icon::history;
    } else {
        state = game_.position().sideToMove() == Color::White ? "Weiß am Zug" : "Schwarz am Zug";
        if (game_.position().inCheck(game_.position().sideToMove()) && settings_.checkGlow) {
            state += ", Schach";
            stateCol = Theme::u32(c.qBlunder);
        }
    }
    if (stateIcon) {
        x += iconAt(x, 22 * s, stateCol, *stateIcon) + 8 * s;
    } else {
        // Farbscheibe der Seite am Zug
        const bool white = game_.position().sideToMove() == Color::White;
        dl->AddCircleFilled(ImVec2(wp.x + x + 8 * s, midY), 8 * s, Theme::u32(white ? c.whiteFill : c.blackFill));
        dl->AddCircle(ImVec2(wp.x + x + 8 * s, midY), 8 * s, Theme::u32(c.textMuted), 0, 1.2f * s);
        x += 24 * s;
    }
    x += textAt(x, sf, fs, stateCol, state.c_str()) + 26 * s;

    if (clock_.control().enabled()) {
        x += iconAt(x, 20 * s, Theme::u32(c.textMuted), icon::timer) + 5 * s;
        x += textAt(x, hf, 18 * s, Theme::u32(c.textMuted), clock_.control().label().c_str()) + 26 * s;
    }
    if (bot_) {
        const bool rated = isRated();
        const ImVec2 a(wp.x + x, wp.y);
        x += iconAt(x, 20 * s, Theme::u32(rated ? c.qBest : c.textMuted), rated ? icon::rated : icon::unrated) + 5 * s;
        x += textAt(x, f, fs, Theme::u32(c.textMuted), rated ? "Gewertet" : "Ungewertet");
        if (ImGui::IsMouseHoveringRect(a, ImVec2(wp.x + x, wp.y + height)))
            ImGui::SetTooltip("%s", rated ? "Diese Partie zählt für deine Elo." : unratedReason().c_str());
    }

    // --- rechts: Zen, Design, Engine, Bilanz, Elo (von rechts nach links) --------
    float xr = W - 14 * s - btn;
    ImGui::SetCursorPos(ImVec2(xr, btnY));
    if (iconButton("##zen", icon::fullscreen, "Zen-Modus: nur das Brett (Z)")) zen_ = true;
    xr -= btn + 4 * s;
    ImGui::SetCursorPos(ImVec2(xr, btnY));
    if (iconButton("##theme", settings_.darkTheme ? icon::lightMode : icon::darkMode,
                   settings_.darkTheme ? "Helles Design" : "Dunkles Design")) {
        settings_.darkTheme = !settings_.darkTheme;
        applyTheme();
        saveSettings();
    }
    xr -= btn + 4 * s;
    ImGui::SetCursorPos(ImVec2(xr, btnY));
    ImGui::PushStyleColor(ImGuiCol_Text, stockfish_ ? c.accent : c.textMuted);
    iconButton("##engine", icon::engine, engineInfo_.c_str());
    ImGui::PopStyleColor(2);  // Text + Button (transparent) von oben
    xr -= 24 * s;

    // Bilanz: Siege, Remis, Niederlagen als drei farbige Zahlen
    const std::string wins = std::to_string(player_.wins), draws = std::to_string(player_.draws),
                      losses = std::to_string(player_.losses);
    const float gapN = 12 * s;
    const float wW = hf->CalcTextSizeA(19 * s, FLT_MAX, 0.0f, wins.c_str()).x;
    const float dW = hf->CalcTextSizeA(19 * s, FLT_MAX, 0.0f, draws.c_str()).x;
    const float lW = hf->CalcTextSizeA(19 * s, FLT_MAX, 0.0f, losses.c_str()).x;
    const float recordW = 22 * s + 6 * s + wW + gapN + dW + gapN + lW;
    float xb = xr - recordW;
    const float recordX = xb;
    xb += iconAt(xb, 22 * s, Theme::u32(c.textMuted), icon::trophy) + 6 * s;
    xb += textAt(xb, hf, 19 * s, Theme::u32(c.qBest), wins.c_str()) + gapN;
    xb += textAt(xb, hf, 19 * s, Theme::u32(c.textMuted), draws.c_str()) + gapN;
    textAt(xb, hf, 19 * s, Theme::u32(c.qBlunder), losses.c_str());
    if (ImGui::IsMouseHoveringRect(ImVec2(wp.x + recordX, wp.y), ImVec2(wp.x + recordX + recordW, wp.y + height)))
        ImGui::SetTooltip("Siege, Remis und Niederlagen gegen Bots\n%d gewertete Partien", player_.games);
    xr = recordX - 30 * s;

    // Elo: Abzeichen, Zahl, Trend
    const std::string elo = std::to_string(player_.rating);
    std::string delta;
    const Icon* trend = &icon::trendFlat;
    ImU32 trendCol = Theme::u32(c.textMuted);
    if (player_.games > 0) {
        delta = signedInt(player_.lastDelta);
        if (player_.lastDelta > 0) trend = &icon::trendUp, trendCol = Theme::u32(c.qBest);
        if (player_.lastDelta < 0) trend = &icon::trendDown, trendCol = Theme::u32(c.qBlunder);
    }
    const float eloW = hf->CalcTextSizeA(25 * s, FLT_MAX, 0.0f, elo.c_str()).x;
    const float trendW = player_.games > 0 ? 20 * s + 3 * s + f->CalcTextSizeA(fs, FLT_MAX, 0.0f, delta.c_str()).x : 0;
    const float blockW = 24 * s + 6 * s + eloW + (trendW > 0 ? 10 * s + trendW : 0);
    float xe = xr - blockW;
    const float blockX = xe;
    xe += iconAt(xe, 24 * s, Theme::u32(c.accent), icon::rating) + 6 * s;
    xe += textAt(xe, hf, 25 * s, Theme::u32(c.text), elo.c_str()) + 10 * s;
    if (player_.games > 0) {
        xe += iconAt(xe, 20 * s, trendCol, *trend) + 3 * s;
        textAt(xe, f, fs, trendCol, delta.c_str());
    }
    if (ImGui::IsMouseHoveringRect(ImVec2(wp.x + blockX, wp.y), ImVec2(wp.x + blockX + blockW, wp.y + height)))
        ImGui::SetTooltip("Deine Elo, lokal gegen Bots\nHöchstwert %d, %d Partien", player_.peak, player_.games);
    (void)x;
}

// ===========================================================================
// Linke Navigation
// ===========================================================================

void GuiApp::drawNav(float width) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float H = ImGui::GetWindowHeight();
    dl->AddLine(ImVec2(wp.x + width - 1, wp.y), ImVec2(wp.x + width - 1, wp.y + H), Theme::u32(c.border));

    struct Item {
        Mode mode;
        const Icon* glyph;
        const char* label;
    };
    const Item items[] = {
        {Mode::Play, &icon::play, "Spielen"},         {Mode::Analysis, &icon::analysis, "Analyse"},
        {Mode::Training, &icon::training, "Training"}, {Mode::Bots, &icon::bot, "Bots"},
        {Mode::Games, &icon::games, "Partien"},       {Mode::Stats, &icon::stats, "Statistik"},
        {Mode::Plugins, &icon::plugin, "Plugins"},    {Mode::Settings, &icon::settings, "Einstellungen"},
    };
    ImGui::SetCursorPos(ImVec2(0, 12 * s));
    for (const Item& it : items)
        if (navItem(*it.glyph, it.label, mode_ == it.mode, width)) mode_ = it.mode;

    // Unten: Design, Zen, Ein-/Ausklappen
    // Drei Eintraege plus Abstand zum unteren Rand; dieselbe Hoehe wie navItem, sonst wird abgeschnitten
    ImGui::SetCursorPos(ImVec2(0, H - 3 * kNavItemHeight * s - 16 * s));
    if (navItem(settings_.darkTheme ? icon::lightMode : icon::darkMode, settings_.darkTheme ? "Hell" : "Dunkel", false,
                width)) {
        settings_.darkTheme = !settings_.darkTheme;
        applyTheme();
        saveSettings();
    }
    if (navItem(icon::fullscreen, "Zen-Modus", false, width)) zen_ = true;
    if (navItem(settings_.sidebarExpanded ? icon::menuOpen : icon::menu,
                settings_.sidebarExpanded ? "Einklappen" : "Ausklappen", false, width)) {
        settings_.sidebarExpanded = !settings_.sidebarExpanded;
        saveSettings();
    }
}

// ===========================================================================
// Rechtes Panel
// ===========================================================================

void GuiApp::drawRightPanel() {
    const Palette& c = theme_.p();
    const float footer = ImGui::GetTextLineHeightWithSpacing() + 4 * theme_.scale;
    ImGui::BeginChild("##content", ImVec2(0, ImGui::GetContentRegionAvail().y - footer), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    switch (mode_) {
        case Mode::Play: panelPlay(); break;
        case Mode::Analysis:
            panelHeader(icon::analysis, "Analyse");
            tabAnalysis();
            break;
        case Mode::Bots: panelBots(); break;
        case Mode::Games:
            panelHeader(icon::games, "Partien");
            tabGames();
            break;
        case Mode::Training:
            panelHeader(icon::training, "Training");
            panelTraining();
            break;
        case Mode::Stats:
            panelHeader(icon::stats, "Statistik");
            panelStats();
            break;
        case Mode::Plugins:
            panelHeader(icon::plugin, "Plugins");
            panelPlugins();
            break;
        case Mode::Settings:
            panelHeader(icon::settings, "Einstellungen");
            tabSettings();
            break;
    }
    ImGui::EndChild();

    if (ImGui::GetTime() < statusUntil_) {
        ImGui::TextColored(c.textMuted, "%s  %s", static_cast<const char*>(icon::info), status_.c_str());
    }
}

void GuiApp::panelPlay() {
    const float s = theme_.scale;
    const float w = ImGui::GetContentRegionAvail().x / 2;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
    if (iconTab(icon::add, "Neue Partie", playTab_ == 0, w)) playTab_ = 0;
    ImGui::SameLine();
    if (iconTab(icon::history, "Partie", playTab_ == 1, w)) playTab_ = 1;
    ImGui::PopStyleVar();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y - 4 * s), ImVec2(p.x + 2 * w, p.y - 4 * s),
                                        Theme::u32(theme_.p().border));
    ImGui::Dummy(ImVec2(0, 4 * s));
    if (playTab_ == 0)
        playSetup();
    else
        playMoves();
}

// ===========================================================================
// Bausteine: Abschnittstitel, Schalter, Zeit-Kacheln, Bot-Zeilen
// ===========================================================================

void GuiApp::sectionTitle(const char* text) {
    const float s = theme_.scale;
    ImGui::Dummy(ImVec2(0, 6 * s));
    ImGui::PushFont(theme_.strong, 16.0f);
    ImGui::TextColored(theme_.p().textMuted, "%s", text);
    ImGui::PopFont();
}

bool GuiApp::toggleRow(const char* label, bool* value, const char* help, float indent) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const float h = 38.0f * s;
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##toggle", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::PopID();
    if (clicked) *value = !*value;

    // GetColorU32 beruecksichtigt die Transparenz von BeginDisabled
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), ImGui::GetColorU32(alpha(c.surfaceRaised, 0.7f)), 6 * s);
    const float midY = p0.y + h * 0.5f;
    ImFont* f = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const float textH = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, label).y;
    dl->AddText(f, fs, ImVec2(p0.x + 8 * s + indent, midY - textH * 0.5f), ImGui::GetColorU32(c.text), label);

    // Schalter rechts: Spur + Knopf
    const float tw = 40 * s, th = 22 * s;
    const ImVec2 t0(p0.x + w - tw - 8 * s, midY - th * 0.5f);
    dl->AddRectFilled(t0, ImVec2(t0.x + tw, t0.y + th), ImGui::GetColorU32(*value ? c.accent : c.border), th * 0.5f);
    const float kr = th * 0.5f - 3 * s;
    const ImVec2 knob(*value ? t0.x + tw - th * 0.5f : t0.x + th * 0.5f, midY);
    dl->AddCircleFilled(knob, kr, ImGui::GetColorU32(*value ? c.accentText : c.textMuted));

    if (help && hovered) ImGui::SetTooltip("%s", help);
    return clicked;
}

bool GuiApp::timeChip(const char* id, const char* big, const char* small, bool selected, ImVec2 size) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    const ImVec4 bg = selected ? alpha(c.accent, 0.18f) : hovered ? c.surfaceRaised : alpha(c.surfaceRaised, 0.55f);
    dl->AddRectFilled(p0, p1, Theme::u32(bg), 8 * s);
    if (selected) dl->AddRect(p0, p1, Theme::u32(c.accent), 8 * s, 0, 1.5f * s);

    ImFont* hf = theme_.heading ? theme_.heading : ImGui::GetFont();
    const float bs = 20 * s;
    const ImVec2 bsz = hf->CalcTextSizeA(bs, FLT_MAX, 0.0f, big);
    const bool hasSmall = small && *small;
    const float by = hasSmall ? p0.y + size.y * 0.40f : p0.y + size.y * 0.5f;
    dl->AddText(hf, bs, ImVec2(p0.x + (size.x - bsz.x) * 0.5f, by - bsz.y * 0.5f),
                Theme::u32(selected ? c.accent : c.text), big);
    if (hasSmall) {
        ImFont* f = ImGui::GetFont();
        const float ss = 13.5f * s;
        const ImVec2 ssz = f->CalcTextSizeA(ss, FLT_MAX, 0.0f, small);
        dl->AddText(f, ss, ImVec2(p0.x + (size.x - ssz.x) * 0.5f, p0.y + size.y * 0.74f - ssz.y * 0.5f),
                    Theme::u32(c.textMuted), small);
    }
    return clicked;
}

void GuiApp::drawBotAvatar(ImDrawList* dl, ImVec2 center, float radius, const BotProfile* p) const {
    const Palette& c = theme_.p();
    if (!p) {
        dl->AddCircleFilled(center, radius, Theme::u32(c.surfaceRaised));
        dl->AddCircle(center, radius, Theme::u32(c.border), 0, 1.2f * theme_.scale);
        Theme::glyphCentered(dl, theme_.body, radius * 1.15f, center, Theme::u32(c.textMuted), icon::person);
        return;
    }
    // Farbe und Figur zeigen die Staerke: Salbei/Bauer fuer Einsteiger bis Messing/Koenig fuer Meister
    const int elo = estimatedBotElo(*p, stockfish_ != nullptr);
    const float t = std::clamp((elo - 400) / 2600.0f, 0.0f, 1.0f);
    const ImVec4 a = c.boardDark, b = c.accent;
    const ImVec4 bg(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
    dl->AddCircleFilled(center, radius, Theme::u32(bg));
    dl->AddCircle(center, radius, IM_COL32(0, 0, 0, 60), 0, 1.0f * theme_.scale);
    // Figur wie auf dem Brett (helle Fuellung, dunkle Kontur), damit sie auf jedem Hintergrund lesbar ist
    static const char* ids[] = {"pawn", "knight", "bishop", "rook", "queen", "king"};
    const int tier = elo < 800 ? 0 : elo < 1200 ? 1 : elo < 1600 ? 2 : elo < 2000 ? 3 : elo < 2600 ? 4 : 5;
    PieceType piece;
    piece.id = ids[tier];
    BoardView::drawPiece(dl, theme_, piece, Color::White, ImVec2(center.x, center.y + radius * 0.02f), radius * 1.75f);
}

bool GuiApp::botRow(const char* id, const BotProfile* p, bool selected, bool showDescription) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const float w = ImGui::GetContentRegionAvail().x;
    const float pad = 10 * s, r = 21 * s;
    const float eloW = 64 * s;
    const float x0off = pad + 2 * r + 12 * s;
    const float textW = w - x0off - eloW - pad;

    const std::string name = p ? p->name : "Zu zweit am Brett";
    const std::string desc = p ? p->description : "Zwei Spieler am selben Rechner. Wird nicht gewertet.";
    ImFont* sf = theme_.strong ? theme_.strong : ImGui::GetFont();
    ImFont* f = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const float nameH = sf->CalcTextSizeA(17 * s, FLT_MAX, 0.0f, name.c_str()).y;
    const float descFs = 15.5f * s;
    const float descH = showDescription ? f->CalcTextSizeA(descFs, FLT_MAX, textW, desc.c_str()).y : 0.0f;
    const float h = std::max(2 * r + 2 * pad, pad + nameH + (showDescription ? 3 * s + descH : 0.0f) + pad);
    (void)fs;

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + w, p0.y + h);
    if (selected) {
        dl->AddRectFilled(p0, p1, Theme::u32(alpha(c.accent, 0.10f)), 8 * s);
        dl->AddRectFilled(ImVec2(p0.x, p0.y + 8 * s), ImVec2(p0.x + 3 * s, p1.y - 8 * s), Theme::u32(c.accent), 2 * s);
    } else if (hovered) {
        dl->AddRectFilled(p0, p1, Theme::u32(alpha(c.surfaceRaised, 0.7f)), 8 * s);
    }

    drawBotAvatar(dl, ImVec2(p0.x + pad + r, p0.y + pad + r), r, p);
    const float x0 = p0.x + x0off;
    dl->AddText(sf, 17 * s, ImVec2(x0, p0.y + pad), Theme::u32(c.text), name.c_str());
    if (showDescription)
        dl->AddText(f, descFs, ImVec2(x0, p0.y + pad + nameH + 3 * s), Theme::u32(c.textMuted), desc.c_str(), nullptr,
                    textW);

    if (p) {
        ImFont* hf = theme_.heading ? theme_.heading : sf;
        const std::string elo = std::to_string(estimatedBotElo(*p, stockfish_ != nullptr));
        const ImVec2 esz = hf->CalcTextSizeA(22 * s, FLT_MAX, 0.0f, elo.c_str());
        dl->AddText(hf, 22 * s, ImVec2(p1.x - pad - esz.x, p0.y + pad - 2 * s), Theme::u32(c.text), elo.c_str());
        const ImVec2 lsz = f->CalcTextSizeA(13 * s, FLT_MAX, 0.0f, "Elo");
        dl->AddText(f, 13 * s, ImVec2(p1.x - pad - lsz.x, p0.y + pad + esz.y - 2 * s), Theme::u32(c.textMuted), "Elo");
    }
    if (std::string_view(id) == "##opp") {
        // Einzelne Auswahlzeile (neue Partie): als Feld mit Pfeil, damit klar ist, dass man wechseln kann
        dl->AddRect(p0, p1, Theme::u32(hovered ? c.accent : c.border, hovered ? 0.8f : 1.0f), 8 * s, 0, 1.0f * s);
    } else if (!selected) {
        dl->AddLine(ImVec2(x0, p1.y), ImVec2(p1.x - pad, p1.y), Theme::u32(alpha(c.border, 0.8f)), 1.0f);
    }
    return clicked;
}

std::string GuiApp::figurine(const std::string& san) const {
    if (!theme_.pieces) return san;
    std::string out;
    for (char ch : san) {
        // Gefuellte Figurensymbole: bei Lesegroesse deutlich klarer als die Umriss-Varianten
        switch (ch) {
            case 'K': out += "\xE2\x99\x9A"; break;
            case 'Q': out += "\xE2\x99\x9B"; break;
            case 'R': out += "\xE2\x99\x9C"; break;
            case 'B': out += "\xE2\x99\x9D"; break;
            case 'N': out += "\xE2\x99\x9E"; break;
            default: out += ch;
        }
    }
    return out;
}

// ===========================================================================
// Spielen: neue Partie
// ===========================================================================

void GuiApp::playSetup() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    ImGui::BeginChild("##setup", ImVec2(0, 0), ImGuiChildFlags_None);
    const float spX = ImGui::GetStyle().ItemSpacing.x;

    // --- Beim letzten Mal offen gebliebene Partie -----------------------------------------
    if (!resumeText_.empty()) {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = 64 * s;
        ImGui::InvisibleButton("##resume", ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), Theme::u32(alpha(c.accent, hovered ? 0.22f : 0.14f)), 8 * s);
        dl->AddRect(p0, ImVec2(p0.x + w, p0.y + h), Theme::u32(alpha(c.accent, 0.6f)), 8 * s);
        Theme::glyphCentered(dl, theme_.body, 26 * s, ImVec2(p0.x + 24 * s, p0.y + h * 0.5f), Theme::u32(c.accent),
                             icon::playCircle);
        dl->AddText(theme_.strong, 17 * s, ImVec2(p0.x + 48 * s, p0.y + 11 * s), Theme::u32(c.text), "Partie fortsetzen");
        dl->AddText(ImGui::GetFont(), 15 * s, ImVec2(p0.x + 48 * s, p0.y + 34 * s), Theme::u32(c.textMuted),
                    resumeInfo_.c_str());
        if (clicked) resumeGame();
    }

    // --- Aktive Plugins: kleine Erinnerung, dass anders gespielt wird -----------------------
    if (!settings_.plugins.empty()) {
        std::string names;
        for (const auto& p : plugins_)
            if (std::find(settings_.plugins.begin(), settings_.plugins.end(), p.id) != settings_.plugins.end())
                names += (names.empty() ? "" : ", ") + p.name;
        ImGui::Dummy(ImVec2(0, 4 * s));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, c.accent);
        if (ImGui::Button((std::string(icon::plugin) + "  Mit Plugins: " + names).c_str())) mode_ = Mode::Plugins;
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Plugins ändern die Regeln. Diese Partien werden nicht gewertet.");
    }

    // --- Bedenkzeit: alle Varianten direkt sichtbar ------------------------------
    sectionTitle("Bedenkzeit");
    const float w4 = (ImGui::GetContentRegionAvail().x - 3 * spX) / 4;
    for (int i = 0; i < kTimePresetCount; ++i) {
        if (i % 4) ImGui::SameLine();
        const TimePreset& p = kTimePresets[i];
        std::string big = p.label, small = p.kind;
        if (p.minutes == 0) big = "frei", small = "ohne Uhr";
        if (p.minutes < 0) big = selectedTimeControl().label(), small = "eigene";
        if (p.minutes < 0 && tcPreset_ != i) big = "…";
        const std::string id = "##tc" + std::to_string(i);
        if (timeChip(id.c_str(), big.c_str(), small.c_str(), tcPreset_ == i, ImVec2(w4, 54 * s))) tcPreset_ = i;
    }
    if (kTimePresets[std::clamp(tcPreset_, 0, kTimePresetCount - 1)].minutes < 0) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##min", &customMinutes_, 0.25f, 180.0f, "%.1f Minuten", ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##inc", &customIncrement_, 0, 60, "+ %d s pro Zug");
    }

    // --- Gegner: eine Zeile, Klick oeffnet die Auswahl -----------------------------
    const bool hotseat = selectedProfile_ < 0 || selectedProfile_ >= static_cast<int>(profiles_.size());
    const BotProfile* opp = hotseat ? nullptr : &profiles_[static_cast<size_t>(selectedProfile_)];
    sectionTitle("Gegner");
    if (!hotseat) {
        const float btn = 22.0f * s + 2 * ImGui::GetStyle().FramePadding.y;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - btn);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (iconButton("##tune", icon::tune, "Diesen Bot anpassen")) mode_ = Mode::Bots;
        ImGui::PopStyleColor();
    }
    const float panelW = ImGui::GetContentRegionAvail().x;
    if (botRow("##opp", opp, false, true)) ImGui::OpenPopup("##pickopp");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gegner wechseln");
    ImGui::SetNextWindowSize(ImVec2(panelW + 16 * s, 0));
    if (ImGui::BeginPopup("##pickopp")) {
        if (botRow("##pickhot", nullptr, hotseat, false)) {
            selectedProfile_ = -1;
            ImGui::CloseCurrentPopup();
        }
        for (size_t i = 0; i < profiles_.size(); ++i) {
            const std::string id = "##pick" + std::to_string(i);
            if (botRow(id.c_str(), &profiles_[i], selectedProfile_ == static_cast<int>(i), false)) {
                selectedProfile_ = static_cast<int>(i);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    // --- Farbe: echte Koenige statt Scheiben ----------------------------------------
    sectionTitle("Du spielst");
    ImGui::BeginDisabled(hotseat);
    const float w3 = (ImGui::GetContentRegionAvail().x - 2 * spX) / 3;
    const char* tips[] = {"Weiß", "Schwarz", "Zufällige Farbe"};
    PieceType king;
    king.id = "king";
    king.symbol = 'K';
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        const bool sel = colorChoice_ == i;
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float bh = 52 * s;
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##col", ImVec2(w3, bh))) colorChoice_ = i;
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::PopID();
        if (hovered) ImGui::SetTooltip("%s", tips[i]);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p1(p0.x + w3, p0.y + bh);
        dl->AddRectFilled(p0, p1, ImGui::GetColorU32(sel ? alpha(c.accent, 0.18f) : alpha(c.surfaceRaised, hovered ? 1.0f : 0.55f)),
                          8 * s);
        if (sel) dl->AddRect(p0, p1, ImGui::GetColorU32(c.accent), 8 * s, 0, 1.5f * s);
        const ImVec2 ctr(p0.x + w3 * 0.5f, p0.y + bh * 0.5f);
        if (i < 2) {
            BoardView::drawPiece(dl, theme_, king, i == 0 ? Color::White : Color::Black, ctr, 44 * s);
        } else {
            BoardView::drawPiece(dl, theme_, king, Color::White, ImVec2(ctr.x - 11 * s, ctr.y), 36 * s);
            BoardView::drawPiece(dl, theme_, king, Color::Black, ImVec2(ctr.x + 11 * s, ctr.y), 36 * s);
        }
    }
    ImGui::EndDisabled();

    // --- Start ---------------------------------------------------------------------
    ImGui::Dummy(ImVec2(0, 10 * s));
    ImGui::PushFont(theme_.strong, 19.0f);
    if (primaryButton(withIcon(icon::start, "Partie starten").c_str(), ImVec2(-FLT_MIN, 54 * s))) {
        startNewGame();
        playTab_ = 1;
    }
    ImGui::PopFont();

    // --- Was die Partie fuer deine Elo bedeutet ---------------------------------------
    if (!hotseat) {
        sectionTitle("Für deine Elo");
        const int botElo = estimatedBotElo(*opp, stockfish_ != nullptr);
        const int d[3] = {eloDelta(player_.rating, botElo, 1.0, player_.games),
                          eloDelta(player_.rating, botElo, 0.5, player_.games),
                          eloDelta(player_.rating, botElo, 0.0, player_.games)};
        const char* labels[3] = {"Sieg", "Remis", "Niederlage"};
        const ImVec4 cols[3] = {c.qBest, c.textMuted, c.qBlunder};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float cw = ImGui::GetContentRegionAvail().x / 3;
        ImFont* hf = theme_.heading ? theme_.heading : ImGui::GetFont();
        for (int i = 0; i < 3; ++i) {
            const std::string v = signedInt(d[i]);
            const float x = p0.x + i * cw;
            dl->AddText(hf, 24 * s, ImVec2(x, p0.y), Theme::u32(cols[i]), v.c_str());
            dl->AddText(ImGui::GetFont(), 14 * s, ImVec2(x, p0.y + 28 * s), Theme::u32(c.textMuted), labels[i]);
        }
        ImGui::Dummy(ImVec2(0, 48 * s));
        if (!stockfish_) ImGui::TextDisabled("Ohne Stockfish spielt die eingebaute Engine (schwächer).");
    }
    ImGui::EndChild();
}

// ===========================================================================
// Spielen: laufende Partie
// ===========================================================================

void GuiApp::playMoves() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const int n = game_.ply();
    const Position& start = game_.startPosition();
    const int offset = start.sideToMove() == Color::Black ? 1 : 0;
    const int rows = (n + offset + 1) / 2;

    const float iconH = 22.0f * s + 2 * ImGui::GetStyle().FramePadding.y;
    const float spacingY = ImGui::GetStyle().ItemSpacing.y;
    const float controlsH = 2 * (iconH + spacingY) + (bot_ ? ImGui::GetTextLineHeightWithSpacing() + 8 * s : 0) + 10 * s;
    ImGui::BeginChild("##moves", ImVec2(0, ImGui::GetContentRegionAvail().y - controlsH), ImGuiChildFlags_None);
    // Eroeffnung bzw. Variante ueber der Zugliste (bei Plugins schon vor dem ersten Zug)
    {
        std::string head = pluginGame_ ? game_.variant().name : std::string();
        if (!pluginGame_ && !fenGame_ && n > 0)
            if (const OpeningMatch m = identifyOpening(game_.sanMoves()); m.opening) head = m.opening->name;
        if (!head.empty()) {
            ImGui::PushFont(theme_.strong, 15.0f);
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(c.textMuted, "%s", head.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
        }
    }
    if (n == 0) {
        ImGui::Dummy(ImVec2(0, 10 * s));
        ImGui::PushTextWrapPos(0);
        const char* first = game_.startPosition().fullmoveNumber() > 1
                                ? "Die Partie beginnt in einer vorgegebenen Stellung. Schau sie dir in Ruhe an: Wo "
                                  "stehen die Figuren, was droht, welcher Zug passt?"
                            : bot_ ? "Mach den ersten Zug – oder warte auf den Bot."
                                   : "Zieh eine Figur, um die Partie zu beginnen.";
        ImGui::TextColored(c.textMuted, "%s", first);
        ImGui::PopTextWrapPos();
    }
    if (n > 0 && ImGui::BeginTable("##movetable", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34 * s);
        // Gleich breit, unabhaengig vom Inhalt (beginnt die Partie mit Schwarz, steht links nur "…")
        ImGui::TableSetupColumn("w", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        const int current = viewPly() - 1;
        for (int row = 0; row < rows; ++row) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 28 * s);
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d", start.fullmoveNumber() + row);
            for (int col = 0; col < 2; ++col) {
                ImGui::TableSetColumnIndex(1 + col);
                const int i = row * 2 + col - offset;
                if (i < 0) {
                    ImGui::TextDisabled("…");
                    continue;
                }
                if (i >= n) continue;
                std::string label = figurine(game_.sanMoves()[static_cast<size_t>(i)]);
                const MoveReview* r = reviewFor(i);
                bool colored = false;
                if (r && settings_.moveMarks) {
                    label += qualityMark(r->quality);
                    if (isError(r->quality) || r->quality == MoveQuality::Brilliant || r->quality == MoveQuality::Great) {
                        ImGui::PushStyleColor(ImGuiCol_Text, theme_.quality(r->quality));
                        colored = true;
                    }
                }
                label += "##m" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), i == current)) setView(i + 1);
                if (colored) ImGui::PopStyleColor();
            }
        }
        if (n != lastScrolledPly_ && isLive()) {
            ImGui::SetScrollHereY(1.0f);
            lastScrolledPly_ = n;
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0, 4 * s));

    // --- Navigation als eine zusammenhaengende Leiste -------------------------------
    {
        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + iconH), Theme::u32(c.surfaceRaised), 8 * s);
        const float w4 = w / 4;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (iconButton("##first", icon::first, "Zum Anfang (Pos1)", false, w4, viewPly() > 0)) setView(0);
        ImGui::SameLine(0, 0);
        if (iconButton("##prev", icon::prev, "Zug zurück (←)", false, w4, viewPly() > 0)) setView(viewPly() - 1);
        ImGui::SameLine(0, 0);
        if (iconButton("##next", icon::next, "Zug vor (→)", false, w4, viewPly() < n)) setView(viewPly() + 1);
        ImGui::SameLine(0, 0);
        if (iconButton("##last", icon::last, "Aktuelle Stellung (Ende)", false, w4, !isLive())) setView(n);
        ImGui::PopStyleColor();
    }

    // --- Aktionen: leichte Icons ohne Kasten; Aufgeben abgesetzt rechts ----------------
    struct Action {
        const char* id;
        const Icon* glyph;
        std::string tip;
        bool enabled;
        int what;
    };
    const bool over = game_.outcome().over();
    const bool hintPossible = !over && (!bot_ || game_.position().sideToMove() != botColor_) && !engineBusy();
    std::vector<Action> actions;
    actions.push_back({"##undo", &icon::undo,
                       settings_.undo ? "Zug zurücknehmen (Strg+Z)" : "Zurücknehmen ist in den Einstellungen aus",
                       settings_.undo && n > 0, 0});
    actions.push_back({"##flip", &icon::flip, "Brett drehen (F)", true, 1});
    if (settings_.hintButton)
        actions.push_back({"##hint", &icon::hint,
                           hintRunning() ? "Engine überlegt …"
                                         : std::string("Tipp zeigen (H)") + (bot_ ? ", Partie wird ungewertet" : ""),
                           hintPossible, 2});
    if (settings_.annotations)
        actions.push_back({"##clear", &icon::clear, "Eigene Pfeile und Kreise löschen", !board_.annotations().empty(), 3});
    actions.push_back({"##draw", &icon::draw, bot_ ? "Remis anbieten" : "Remis vereinbaren", !over && n >= 2, 4});

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    for (size_t i = 0; i < actions.size(); ++i) {
        if (i) ImGui::SameLine(0, 4 * s);
        const Action& a = actions[i];
        if (!iconButton(a.id, *a.glyph, a.tip.c_str(), false, 0, a.enabled)) continue;
        switch (a.what) {
            case 0: undo(); break;
            case 1: flipped_ = !flipped_; break;
            case 2: startHint(); break;
            case 3: board_.clearAnnotations(); break;
            case 4: offerDraw(); break;
        }
    }
    const float resignW = ImGui::CalcTextSize("Aufgeben").x + 44 * s;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - resignW);
    ImGui::BeginDisabled(over || n == 0);
    ImGui::PushStyleColor(ImGuiCol_Text, c.textMuted);
    if (ImGui::Button(withIcon(icon::flag, "Aufgeben").c_str(), ImVec2(resignW, iconH))) ImGui::OpenPopup("##resign");
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    ImGui::PopStyleColor();
    if (ImGui::BeginPopup("##resign")) {
        ImGui::TextUnformatted("Wirklich aufgeben?");
        if (primaryButton(withIcon(icon::flag, "Aufgeben").c_str())) {
            resign();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Weiterspielen")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Bot-Begruendung verraet die Bewertung – abschaltbar
    if (bot_) {
        // Nur der Zug bekommt Figurensymbole, nicht der Text ("Nicht ganz genau" bliebe sonst nicht lesbar)
        std::string info = figurine(lastBotSan_);
        if (settings_.botInfo && lastBotInfo_.size() > lastBotSan_.size()) info += lastBotInfo_.substr(lastBotSan_.size());
        if (!lastBotSan_.empty()) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(c.textMuted, "%s: %s", bot_->profile().name.c_str(), info.c_str());
            ImGui::PopTextWrapPos();
        }
    }
}

// ===========================================================================
// Bots
// ===========================================================================

void GuiApp::panelBots() {
    const float s = theme_.scale;
    panelHeader(icon::bot, "Bots");
    ImGui::BeginChild("##bots", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Die Elo ist geschätzt. Klick auf einen Bot, um ihn auszuwählen.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, 4 * s));
    for (size_t i = 0; i < profiles_.size(); ++i) {
        const std::string id = "##bot" + std::to_string(i);
        if (botRow(id.c_str(), &profiles_[i], selectedProfile_ == static_cast<int>(i), true))
            selectedProfile_ = static_cast<int>(i);
    }
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::Button((std::string(icon::personAdd) + "  Eigenen Bot anlegen").c_str())) createOwnBot();
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Kopie des gewählten Bots – Name, Stärke und Stil frei einstellbar");

    if (selectedProfile_ >= 0 && selectedProfile_ < static_cast<int>(profiles_.size())) {
        ImGui::Dummy(ImVec2(0, 8 * s));
        const std::string label =
            withIcon(icon::start, "Gegen " + profiles_[static_cast<size_t>(selectedProfile_)].name + " spielen");
        ImGui::PushFont(theme_.strong, 0.0f);
        if (primaryButton(label.c_str(), ImVec2(-FLT_MIN, 46 * s))) {
            mode_ = Mode::Play;
            playTab_ = 0;  // Zeit und Farbe noch waehlen, dann starten
        }
        ImGui::PopFont();
        sectionTitle("Anpassen");
        botEditor();
    }
    ImGui::EndChild();
}

}  // namespace chess::ui
