// Rahmen der Oberflaeche: Top-Leiste, linke Navigation, rechtes Panel.
// Moeglichst Icons (Google Material Icons) statt Text; jede Schaltflaeche hat einen Tooltip.

#include "ui/gui_app.hpp"

#include "core/notation.hpp"
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

std::string signedInt(int v) { return (v > 0 ? "+" : v < 0 ? "−" : "±") + std::to_string(std::abs(v)); }

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
    const float h = 48.0f * s;
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
        {Mode::Play, &icon::play, "Spielen"},       {Mode::Analysis, &icon::analysis, "Analyse"},
        {Mode::Bots, &icon::bot, "Bots"},           {Mode::Games, &icon::games, "Partien"},
        {Mode::Settings, &icon::settings, "Einstellungen"},
    };
    ImGui::SetCursorPos(ImVec2(0, 12 * s));
    for (const Item& it : items)
        if (navItem(*it.glyph, it.label, mode_ == it.mode, width)) mode_ = it.mode;

    // Unten: Design, Zen, Ein-/Ausklappen
    ImGui::SetCursorPos(ImVec2(0, H - 3 * 46.0f * s - 12 * s));
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
            panelHeader(icon::games, "Partien & Export");
            tabExport();
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

void GuiApp::playSetup() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    ImGui::BeginChild("##setup", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14 * s, 11 * s));

    // --- Bedenkzeit ------------------------------------------------------------
    ImGui::TextDisabled("%s  Bedenkzeit", static_cast<const char*>(icon::timer));
    const TimePreset& tp = kTimePresets[std::clamp(tcPreset_, 0, kTimePresetCount - 1)];
    auto presetText = [](const TimePreset& p) {
        std::string t = p.label;
        if (*p.kind) t += "  " + std::string(p.kind);
        return t;
    };
    std::string preview = tp.minutes < 0 ? selectedTimeControl().label() + "  eigene Zeit" : presetText(tp);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##tc", withIcon(tp.minutes == 0 ? icon::stop : icon::timer, preview).c_str(),
                          ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < kTimePresetCount; ++i) {
            const Icon& ic = kTimePresets[i].minutes == 0 ? icon::stop
                             : kTimePresets[i].minutes < 0 ? icon::tune
                             : kTimePresets[i].minutes <= 1 ? icon::bolt
                                                            : icon::timer;
            if (ImGui::Selectable(withIcon(ic, presetText(kTimePresets[i])).c_str(), tcPreset_ == i)) tcPreset_ = i;
        }
        ImGui::EndCombo();
    }
    if (tp.minutes < 0) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * s, 7 * s));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##min", &customMinutes_, 0.25f, 180.0f, "%.1f Minuten", ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##inc", &customIncrement_, 0, 60, "+ %d s pro Zug");
        ImGui::PopStyleVar();
    }

    // --- Gegner -----------------------------------------------------------------
    ImGui::Dummy(ImVec2(0, 4 * s));
    ImGui::TextDisabled("%s  Gegner", static_cast<const char*>(icon::bot));
    auto botText = [&](const BotProfile& p) {
        return p.name + "   ≈ " + std::to_string(estimatedBotElo(p, stockfish_ != nullptr));
    };
    const bool hotseat = selectedProfile_ < 0 || selectedProfile_ >= static_cast<int>(profiles_.size());
    const std::string oppPreview = hotseat ? withIcon(icon::person, "Zu zweit am Brett")
                                           : withIcon(icon::bot, botText(profiles_[static_cast<size_t>(selectedProfile_)]));
    const float tuneW = 22.0f * s + 2 * ImGui::GetStyle().FramePadding.y;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - tuneW - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::BeginCombo("##opp", oppPreview.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable(withIcon(icon::person, "Zu zweit am Brett").c_str(), hotseat)) selectedProfile_ = -1;
        for (size_t i = 0; i < profiles_.size(); ++i)
            if (ImGui::Selectable(withIcon(icon::bot, botText(profiles_[i])).c_str(),
                                  selectedProfile_ == static_cast<int>(i)))
                selectedProfile_ = static_cast<int>(i);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (iconButton("##tune", icon::tune, "Bot anpassen", false, tuneW, !hotseat)) mode_ = Mode::Bots;

    // --- Farbe ------------------------------------------------------------------
    ImGui::Dummy(ImVec2(0, 4 * s));
    ImGui::TextDisabled("%s  Du spielst", static_cast<const char*>(icon::person));
    ImGui::BeginDisabled(hotseat);
    const float w3 = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    const char* tips[] = {"Weiß", "Schwarz", "Zufällige Farbe"};
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        const bool sel = colorChoice_ == i;
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Button, sel ? alpha(c.accent, 0.22f) : c.surfaceRaised);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float bh = 46 * s;
        if (ImGui::Button("##col", ImVec2(w3, bh))) colorChoice_ = i;
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tips[i]);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 ctr(p0.x + w3 * 0.5f, p0.y + bh * 0.5f);
        if (sel) dl->AddRect(p0, ImVec2(p0.x + w3, p0.y + bh), Theme::u32(c.accent), 7 * s, 0, 1.5f * s);
        if (i < 2) {
            dl->AddCircleFilled(ctr, 11 * s, Theme::u32(i == 0 ? c.whiteFill : c.blackFill));
            dl->AddCircle(ctr, 11 * s, Theme::u32(c.border), 0, 1.2f * s);
        } else {
            ImFont* f = ImGui::GetFont();
            const ImVec2 isz = f->CalcTextSizeA(24 * s, FLT_MAX, 0.0f, icon::casino);
            dl->AddText(f, 24 * s, ImVec2(ctr.x - isz.x * 0.5f, ctr.y - isz.y * 0.5f), Theme::u32(c.text), icon::casino);
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();

    // --- Start ------------------------------------------------------------------
    ImGui::Dummy(ImVec2(0, 8 * s));
    ImGui::PushFont(theme_.heading, 20.0f);
    if (primaryButton(withIcon(icon::start, "Partie starten").c_str(), ImVec2(-FLT_MIN, 54 * s))) {
        startNewGame();
        playTab_ = 1;
    }
    ImGui::PopFont();

    // --- Infos zum Gegner und zur Wertung ---------------------------------------
    ImGui::Dummy(ImVec2(0, 6 * s));
    if (!hotseat) {
        const BotProfile& p = profiles_[static_cast<size_t>(selectedProfile_)];
        const int botElo = estimatedBotElo(p, stockfish_ != nullptr);
        std::string chip = std::string(tempoModeName(p.tempo.mode));
        if (p.aggression > 0.05) chip += ", aggressiv";
        if (p.aggression < -0.05) chip += ", vorsichtig";
        card("##oppinfo", p.name, chip, p.description, false);
        ImGui::Dummy(ImVec2(0, 2 * s));
        ImGui::TextDisabled("%s  Sieg %s, Remis %s, Niederlage %s Elo",
                            static_cast<const char*>(icon::rating),
                            signedInt(eloDelta(player_.rating, botElo, 1.0, player_.games)).c_str(),
                            signedInt(eloDelta(player_.rating, botElo, 0.5, player_.games)).c_str(),
                            signedInt(eloDelta(player_.rating, botElo, 0.0, player_.games)).c_str());
        if (!stockfish_) ImGui::TextDisabled("Ohne Stockfish spielt die eingebaute Engine (schwächer).");
    } else {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Zwei Spieler am selben Rechner. Zu zweit wird nicht gewertet.");
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

void GuiApp::playMoves() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const int n = game_.ply();
    const Position& start = game_.startPosition();
    const int offset = start.sideToMove() == Color::Black ? 1 : 0;
    const int rows = (n + offset + 1) / 2;

    const float iconH = 22.0f * s + 2 * ImGui::GetStyle().FramePadding.y;
    const float spacingY = ImGui::GetStyle().ItemSpacing.y;
    const float controlsH = 2 * (iconH + spacingY) + (bot_ ? ImGui::GetTextLineHeightWithSpacing() : 0) + 6 * s;
    ImGui::BeginChild("##moves", ImVec2(0, ImGui::GetContentRegionAvail().y - controlsH), ImGuiChildFlags_None);
    if (n == 0) {
        ImGui::Dummy(ImVec2(0, 10 * s));
        ImGui::PushFont(nullptr, 34.0f);
        ImGui::TextColored(c.textMuted, "%s", static_cast<const char*>(icon::play));
        ImGui::PopFont();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", bot_ ? "Mach den ersten Zug – oder warte auf den Bot."
                                       : "Zieh eine Figur, um die Partie zu beginnen.");
        ImGui::PopTextWrapPos();
    } else if (ImGui::BeginTable("##movetable", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 38 * s);
        ImGui::TableSetupColumn("w", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch);
        const int current = viewPly() - 1;
        for (int row = 0; row < rows; ++row) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d.", start.fullmoveNumber() + row);
            for (int col = 0; col < 2; ++col) {
                ImGui::TableSetColumnIndex(1 + col);
                const int i = row * 2 + col - offset;
                if (i < 0) {
                    ImGui::TextDisabled("…");
                    continue;
                }
                if (i >= n) continue;
                std::string label = game_.sanMoves()[static_cast<size_t>(i)];
                const MoveReview* r = reviewFor(i);
                bool colored = false;
                if (r && settings_.moveMarks) {
                    label += qualityMark(r->quality);
                    if (r->quality == MoveQuality::Inaccuracy || r->quality == MoveQuality::Mistake ||
                        r->quality == MoveQuality::Blunder) {
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

    // --- Navigation: 4 gleich breite Icon-Knoepfe -------------------------------
    const float spX = ImGui::GetStyle().ItemSpacing.x;
    const float w4 = (ImGui::GetContentRegionAvail().x - 3 * spX) / 4;
    ImGui::PushStyleColor(ImGuiCol_Button, c.surfaceRaised);
    if (iconButton("##first", icon::first, "Zum Anfang (Pos1)", false, w4)) setView(0);
    ImGui::SameLine();
    if (iconButton("##prev", icon::prev, "Zug zurück (←)", false, w4)) setView(viewPly() - 1);
    ImGui::SameLine();
    if (iconButton("##next", icon::next, "Zug vor (→)", false, w4)) setView(viewPly() + 1);
    ImGui::SameLine();
    if (iconButton("##last", icon::last, "Aktuelle Stellung (Ende)", false, w4)) setView(n);
    ImGui::PopStyleColor();

    // --- Aktionen: nur was eingeschaltet ist -------------------------------------
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
                                         : std::string("Tipp zeigen (H)") + (bot_ ? " – Partie wird ungewertet" : ""),
                           hintPossible, 2});
    if (settings_.annotations)
        actions.push_back({"##clear", &icon::clear, "Eigene Pfeile & Kreise löschen", !board_.annotations().empty(), 3});
    actions.push_back({"##resign", &icon::flag, "Aufgeben", !over && n > 0, 4});

    const float wa = (ImGui::GetContentRegionAvail().x - (actions.size() - 1) * spX) / static_cast<float>(actions.size());
    ImGui::PushStyleColor(ImGuiCol_Button, c.surfaceRaised);
    for (size_t i = 0; i < actions.size(); ++i) {
        if (i) ImGui::SameLine();
        const Action& a = actions[i];
        if (!iconButton(a.id, *a.glyph, a.tip.c_str(), false, wa, a.enabled)) continue;
        switch (a.what) {
            case 0: undo(); break;
            case 1: flipped_ = !flipped_; break;
            case 2: startHint(); break;
            case 3: board_.clearAnnotations(); break;
            case 4: ImGui::OpenPopup("##resign"); break;
        }
    }
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
        const std::string& info = settings_.botInfo ? lastBotInfo_ : lastBotSan_;
        if (!info.empty()) ImGui::TextDisabled("%s  %s", static_cast<const char*>(icon::bot), info.c_str());
    }
}

void GuiApp::panelBots() {
    const float s = theme_.scale;
    panelHeader(icon::bot, "Bots");
    ImGui::BeginChild("##bots", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::TextDisabled("Die Elo der Bots ist geschätzt.");
    ImGui::Dummy(ImVec2(0, 2 * s));
    for (size_t i = 0; i < profiles_.size(); ++i) {
        const BotProfile& p = profiles_[i];
        std::string chip = "≈ " + std::to_string(estimatedBotElo(p, stockfish_ != nullptr));
        const std::string id = "##bot" + std::to_string(i);
        if (card(id.c_str(), p.name, chip, p.description, selectedProfile_ == static_cast<int>(i)))
            selectedProfile_ = static_cast<int>(i);
    }

    if (selectedProfile_ >= 0 && selectedProfile_ < static_cast<int>(profiles_.size())) {
        ImGui::Dummy(ImVec2(0, 4 * s));
        const std::string label = withIcon(icon::start, "Gegen " + profiles_[static_cast<size_t>(selectedProfile_)].name +
                                                             " spielen");
        if (primaryButton(label.c_str(), ImVec2(-FLT_MIN, 44 * s))) {
            mode_ = Mode::Play;
            playTab_ = 0;  // Zeit und Farbe noch waehlen, dann starten
        }
        ImGui::Dummy(ImVec2(0, 2 * s));
        ImGui::SeparatorText((std::string(icon::tune) + "  Anpassen").c_str());
        botEditor();
    }
    ImGui::EndChild();
}

}  // namespace chess::ui
