#pragma once

#include "analysis/analysis.hpp"

#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

namespace chess::ui {

// Farbsystem (siehe docs/DESIGN.md): Graphit, Schiefer, Knochen, Salbei, Messing als einziger Akzent.
// Zugbewertungen haben eigene Farben und kommen nur dort vor.
struct Palette {
    ImVec4 bg, surface, surfaceRaised, border, text, textMuted, accent, accentText;
    ImVec4 boardLight, boardDark, coordOnLight, coordOnDark;
    ImVec4 lastMove, selected, hint, check, arrow;
    ImVec4 whiteFill, whiteOutline, blackFill, blackOutline;
    ImVec4 qBrilliant, qGreat, qBest, qExcellent, qGood, qInaccuracy, qMistake, qMiss, qBlunder, qForced;
};

class Theme {
public:
    bool dark = true;
    float scale = 1.0f;  // DPI-Faktor
    ImFont* body = nullptr;     // Segoe UI – Fliesstext
    ImFont* strong = nullptr;   // Segoe UI Semibold – Beschriftungen, Knoepfe
    ImFont* heading = nullptr;  // Bahnschrift – Ueberschriften, Uhren, Elo, Genauigkeit
    ImFont* pieces = nullptr;   // Schachsymbole (Segoe UI Symbol); nullptr -> Buchstaben
    bool hasIcons = false;
    // Liefert die Textur zu einem Bildpfad (Figurenbilder aus Plugins); 0 = nicht verfuegbar
    std::function<ImTextureID(const std::string&)> pieceImage;
    int pieceSet = 0;  // 0 Klassisch, 1 Diagramm, 2 Buchstaben

    const Palette& p() const { return dark ? darkPalette_ : lightPalette_; }

    // Brettfarben zur Auswahl (Index 0 = Salbei, das Hausdesign)
    struct BoardTheme {
        const char* name;
        ImVec4 light, dark;
    };
    static const std::vector<BoardTheme>& boardThemes();
    void setBoard(int index);

    // Einmal nach ImGui::CreateContext. iconFont: Material Icons (darf fehlen, dann ohne Icons).
    void loadFonts(const std::string& iconFont = {});
    // Stil setzen (auch nach Umschalten hell/dunkel).
    void apply();

    static ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
    static ImU32 u32(const ImVec4& c, float alpha) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, alpha)); }

    ImVec4 quality(MoveQuality q) const;

    // Zeichnet ein Zeichen (z. B. ein Icon) optisch zentriert um `center`:
    // gemessen am tatsaechlichen Umriss des Glyphen, nicht an der Zeilenhoehe.
    static void glyphCentered(ImDrawList* dl, ImFont* font, float size, ImVec2 center, ImU32 col, const char* utf8);

    // Rundes Abzeichen fuer eine Zugbewertung ("!!", Stern, "??" ...).
    void qualityBadge(ImDrawList* dl, MoveQuality q, ImVec2 center, float radius) const;

private:
    Palette darkPalette_ = makeDark();
    Palette lightPalette_ = makeLight();
    static Palette makeDark();
    static Palette makeLight();
};

// Kennzeichnung in der Zugliste wie in Schachbuechern ("!!", "?!", "??").
const char* qualityMark(MoveQuality q);

}  // namespace chess::ui
