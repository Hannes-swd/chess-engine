#include "ui/theme.hpp"

#include "ui/icons.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace chess::ui {

namespace {

ImVec4 hex(unsigned rgb, float a = 1.0f) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

ImVec4 withAlpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

const char* firstExisting(std::initializer_list<const char*> paths) {
    for (const char* p : paths) {
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) return p;
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// Farben (docs/DESIGN.md)
// ---------------------------------------------------------------------------

Palette Theme::makeDark() {
    Palette p{};
    p.bg = hex(0x15171A);             // Graphit
    p.surface = hex(0x1D2024);        // Schiefer
    p.surfaceRaised = hex(0x272B30);
    p.border = hex(0x31363C);
    p.text = hex(0xECE5D6);           // Knochen
    p.textMuted = hex(0xA9AEB5);      // deutlich heller als vorher: lesbar
    p.accent = hex(0xC9A25E);         // Messing
    p.accentText = hex(0x1A1610);

    p.boardLight = hex(0xDCD3C2);
    p.boardDark = hex(0x7A8A82);      // Salbei
    p.coordOnLight = hex(0x6B7A73);
    p.coordOnDark = hex(0xE6DECF);

    p.lastMove = hex(0xC9A25E, 0.38f);
    p.selected = hex(0xC9A25E, 0.62f);
    p.hint = hex(0x1D2024, 0.30f);
    p.check = hex(0xD0605A, 0.70f);
    p.arrow = hex(0xC9A25E, 0.85f);

    p.whiteFill = hex(0xF6F2EA);
    p.whiteOutline = hex(0x1C1E22);
    p.blackFill = hex(0x25282D);
    p.blackOutline = hex(0xE9E4DA, 0.80f);

    p.qBrilliant = hex(0x2FB3A6);
    p.qGreat = hex(0x5C8FD6);
    p.qBest = hex(0x7DAA5A);
    p.qExcellent = hex(0x92B06C);
    p.qGood = hex(0x9EA88F);
    p.qInaccuracy = hex(0xE2BA4A);
    p.qMistake = hex(0xE3893B);
    p.qMiss = hex(0xD9654F);
    p.qBlunder = hex(0xCC463C);
    p.qForced = hex(0x8F959C);
    return p;
}

Palette Theme::makeLight() {
    Palette p = makeDark();
    p.bg = hex(0xEEE9DF);
    p.surface = hex(0xF8F5EF);
    p.surfaceRaised = hex(0xE6E0D4);
    p.border = hex(0xD5CDBF);
    p.text = hex(0x1F2226);
    p.textMuted = hex(0x545A62);
    p.accent = hex(0x94702F);
    p.accentText = hex(0xFBF8F2);
    p.hint = hex(0x1F2226, 0.24f);
    p.arrow = hex(0x94702F, 0.82f);
    p.qBrilliant = hex(0x1C9488);
    p.qGreat = hex(0x3D72BF);
    p.qBest = hex(0x5E8C3E);
    p.qExcellent = hex(0x6E9147);
    p.qGood = hex(0x7A866A);
    p.qInaccuracy = hex(0xB08A16);
    p.qMistake = hex(0xC06A20);
    p.qMiss = hex(0xBE4B37);
    p.qBlunder = hex(0xB0352C);
    p.qForced = hex(0x6B7178);
    return p;
}

// ---------------------------------------------------------------------------
// Schriften
// ---------------------------------------------------------------------------

void Theme::loadFonts(const std::string& iconFont) {
    ImGuiIO& io = ImGui::GetIO();
    const char* ui = firstExisting({"C:/Windows/Fonts/segoeui.ttf"});
    const char* semibold = firstExisting({"C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeuib.ttf"});
    const char* din = firstExisting({"C:/Windows/Fonts/bahnschrift.ttf"});
    const char* symbols = firstExisting({"C:/Windows/Fonts/seguisym.ttf"});
    std::error_code ec;
    hasIcons = !iconFont.empty() && std::filesystem::exists(iconFont, ec);

    // Material Icons in jede Textschrift einmischen, damit Icon und Text in einer Zeile stehen.
    // Reihenfolge wichtig: Segoe UI Symbol belegt ebenfalls U+E000.., der erste Treffer gewinnt.
    auto mergeIcons = [&](float size) {
        if (!hasIcons) return;
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.GlyphOffset = ImVec2(0, size * 0.20f);
        cfg.RasterizerMultiply = 1.1f;
        io.Fonts->AddFontFromFileTTF(iconFont.c_str(), size * 1.18f, &cfg);
    };
    auto add = [&](const char* file, float size, float multiply) -> ImFont* {
        ImFontConfig cfg;
        cfg.RasterizerMultiply = multiply;  // etwas kraeftiger: duenne Striche werden lesbar
        return io.Fonts->AddFontFromFileTTF(file, size, &cfg);
    };

    body = ui ? add(ui, 18.0f, 1.25f) : io.Fonts->AddFontDefault();
    mergeIcons(18.0f);
    if (ui && symbols) {
        ImFontConfig merge;
        merge.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(symbols, 18.0f, &merge);
    }
    strong = semibold ? add(semibold, 18.0f, 1.15f) : body;
    if (semibold) mergeIcons(18.0f);
    heading = din ? add(din, 22.0f, 1.1f) : strong;
    if (din) mergeIcons(22.0f);
    pieces = symbols ? io.Fonts->AddFontFromFileTTF(symbols, 64.0f) : nullptr;
    io.FontDefault = body;
}

// ---------------------------------------------------------------------------
// Stil
// ---------------------------------------------------------------------------

void Theme::apply() {
    const Palette& c = p();
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();

    s.WindowPadding = ImVec2(20, 18);
    s.FramePadding = ImVec2(12, 8);
    s.ItemSpacing = ImVec2(10, 10);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.CellPadding = ImVec2(8, 6);
    s.ScrollbarSize = 10;
    s.GrabMinSize = 12;
    // Rundungen nach Hierarchie: grosse Flaechen weicher, Bedienelemente knapper
    s.WindowRounding = 0;
    s.ChildRounding = 10;
    s.FrameRounding = 6;
    s.PopupRounding = 8;
    s.ScrollbarRounding = 6;
    s.GrabRounding = 4;
    s.TabRounding = 6;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.FrameBorderSize = 0;
    s.PopupBorderSize = 1;
    s.TabBorderSize = 0;
    s.SeparatorTextBorderSize = 1;
    s.SeparatorTextPadding = ImVec2(0, 4);

    ImVec4* col = s.Colors;
    col[ImGuiCol_Text] = c.text;
    col[ImGuiCol_TextDisabled] = c.textMuted;
    col[ImGuiCol_WindowBg] = c.bg;
    col[ImGuiCol_ChildBg] = withAlpha(c.surface, 0.0f);
    col[ImGuiCol_PopupBg] = c.surface;
    col[ImGuiCol_Border] = c.border;
    col[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_FrameBg] = c.surfaceRaised;
    col[ImGuiCol_FrameBgHovered] = withAlpha(c.accent, 0.18f);
    col[ImGuiCol_FrameBgActive] = withAlpha(c.accent, 0.28f);
    col[ImGuiCol_TitleBg] = c.surface;
    col[ImGuiCol_TitleBgActive] = c.surface;
    col[ImGuiCol_MenuBarBg] = c.surface;
    col[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_ScrollbarGrab] = c.border;
    col[ImGuiCol_ScrollbarGrabHovered] = c.textMuted;
    col[ImGuiCol_ScrollbarGrabActive] = c.accent;
    col[ImGuiCol_CheckMark] = c.accent;
    col[ImGuiCol_CheckboxSelectedBg] = c.surfaceRaised;
    col[ImGuiCol_InputTextCursor] = c.accent;
    col[ImGuiCol_TabDimmedSelectedOverline] = c.accent;
    col[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_ResizeGripHovered] = withAlpha(c.accent, 0.4f);
    col[ImGuiCol_ResizeGripActive] = c.accent;
    col[ImGuiCol_TextLink] = c.accent;
    col[ImGuiCol_DragDropTarget] = c.accent;
    col[ImGuiCol_SliderGrab] = c.accent;
    col[ImGuiCol_SliderGrabActive] = c.accent;
    col[ImGuiCol_Button] = c.surfaceRaised;
    col[ImGuiCol_ButtonHovered] = withAlpha(c.accent, 0.22f);
    col[ImGuiCol_ButtonActive] = withAlpha(c.accent, 0.35f);
    col[ImGuiCol_Header] = withAlpha(c.accent, 0.20f);
    col[ImGuiCol_HeaderHovered] = withAlpha(c.accent, 0.14f);
    col[ImGuiCol_HeaderActive] = withAlpha(c.accent, 0.28f);
    col[ImGuiCol_Separator] = c.border;
    col[ImGuiCol_SeparatorHovered] = c.accent;
    col[ImGuiCol_SeparatorActive] = c.accent;
    col[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_TabHovered] = withAlpha(c.accent, 0.14f);
    col[ImGuiCol_TabSelected] = c.surfaceRaised;
    col[ImGuiCol_TabSelectedOverline] = c.accent;
    col[ImGuiCol_TabDimmed] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_TabDimmedSelected] = c.surfaceRaised;
    col[ImGuiCol_TableHeaderBg] = c.surface;
    col[ImGuiCol_TableBorderLight] = c.border;
    col[ImGuiCol_TableBorderStrong] = c.border;
    col[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_TableRowBgAlt] = withAlpha(c.surfaceRaised, 0.45f);
    col[ImGuiCol_PlotHistogram] = c.accent;
    col[ImGuiCol_TextSelectedBg] = withAlpha(c.accent, 0.35f);
    col[ImGuiCol_NavCursor] = c.accent;
    col[ImGuiCol_ModalWindowDimBg] = withAlpha(c.bg, 0.6f);

    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

// ---------------------------------------------------------------------------
// Zugbewertungen
// ---------------------------------------------------------------------------

ImVec4 Theme::quality(MoveQuality q) const {
    const Palette& c = p();
    switch (q) {
        case MoveQuality::Brilliant: return c.qBrilliant;
        case MoveQuality::Great: return c.qGreat;
        case MoveQuality::Best: return c.qBest;
        case MoveQuality::Excellent: return c.qExcellent;
        case MoveQuality::Good: return c.qGood;
        case MoveQuality::Inaccuracy: return c.qInaccuracy;
        case MoveQuality::Mistake: return c.qMistake;
        case MoveQuality::Miss: return c.qMiss;
        case MoveQuality::Blunder: return c.qBlunder;
        case MoveQuality::Forced: return c.qForced;
    }
    return c.text;
}

const char* qualityMark(MoveQuality q) {
    switch (q) {
        case MoveQuality::Brilliant: return "!!";
        case MoveQuality::Great: return "!";
        case MoveQuality::Inaccuracy: return "?!";
        case MoveQuality::Mistake: return "?";
        case MoveQuality::Miss: return "?";
        case MoveQuality::Blunder: return "??";
        default: return "";
    }
}

void Theme::glyphCentered(ImDrawList* dl, ImFont* font, float size, ImVec2 center, ImU32 col, const char* utf8) {
    if (!font || !utf8 || !*utf8) return;
    const auto* u = reinterpret_cast<const unsigned char*>(utf8);
    unsigned cp = u[0];
    if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2]) cp = ((u[0] & 0x0Fu) << 12) | ((u[1] & 0x3Fu) << 6) | (u[2] & 0x3Fu);
    const bool single = (cp >= 0x80 && std::strlen(utf8) == 3) || std::strlen(utf8) == 1;

    ImFontBaked* baked = font->GetFontBaked(size);
    const ImFontGlyph* g = (single && baked) ? baked->FindGlyphNoFallback(static_cast<ImWchar>(cp)) : nullptr;
    ImVec2 pos;
    if (g && g->X1 > g->X0) {
        pos = ImVec2(center.x - (g->X0 + g->X1) * 0.5f, center.y - (g->Y0 + g->Y1) * 0.5f);
    } else {
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, utf8);
        pos = ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f);
    }
    dl->AddText(font, size, ImVec2(std::floor(pos.x + 0.5f), std::floor(pos.y + 0.5f)), col, utf8);
}

void Theme::qualityBadge(ImDrawList* dl, MoveQuality q, ImVec2 center, float radius) const {
    const ImVec4 col = quality(q);
    dl->AddCircleFilled(center + ImVec2(0, radius * 0.12f), radius, IM_COL32(0, 0, 0, 70));
    dl->AddCircleFilled(center, radius, u32(col));
    dl->AddCircle(center, radius, IM_COL32(255, 255, 255, 60), 0, std::max(1.0f, radius * 0.08f));

    const ImU32 fg = IM_COL32(255, 255, 255, 255);
    const char* text = nullptr;
    const char* glyph = nullptr;
    switch (q) {
        case MoveQuality::Brilliant: text = "!!"; break;
        case MoveQuality::Great: text = "!"; break;
        case MoveQuality::Best: glyph = icon::star; break;
        case MoveQuality::Excellent: glyph = icon::thumbUp; break;
        case MoveQuality::Good: glyph = icon::checkPlain; break;
        case MoveQuality::Inaccuracy: text = "?!"; break;
        case MoveQuality::Mistake: text = "?"; break;
        case MoveQuality::Miss: glyph = icon::close; break;
        case MoveQuality::Blunder: text = "??"; break;
        case MoveQuality::Forced: glyph = icon::forward; break;
    }
    if (glyph)
        glyphCentered(dl, body ? body : ImGui::GetFont(), radius * 1.45f, center, fg, glyph);
    else if (text) {
        ImFont* f = heading ? heading : ImGui::GetFont();
        const float fs = radius * (std::strlen(text) > 1 ? 1.15f : 1.35f);
        const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
        dl->AddText(f, fs, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.52f), fg, text);
    }
}

}  // namespace chess::ui
