#pragma once

#include "core/position.hpp"
#include "ui/theme.hpp"

#include <imgui.h>

#include <optional>
#include <vector>

namespace chess::ui {

struct BoardArrow {
    Move move;
    ImU32 color = 0;
};

struct BoardOverlay {
    std::optional<Move> lastMove;
    std::vector<BoardArrow> arrows;  // z. B. bester Zug aus der Analyse, Tipp der Engine
    bool showCoordinates = true;
    bool showHints = true;        // Punkte fuer legale Zuege der ausgewaehlten Figur
    bool showDanger = true;       // Zielfelder, die der Gegner angreift, rot markieren
    bool showCheck = true;        // roter Schein um den Koenig im Schach
    bool showTargetFrame = true;  // Rahmen ums Zielfeld unter der Maus
    bool allowAnnotations = true; // Pfeile/Kreise per Rechtsklick
    std::optional<Color> hangingFor;  // gesetzt: ungedeckte, angegriffene Figuren dieser Farbe markieren
    // Bewertungs-Abzeichen (Analyse) in der Ecke eines Feldes
    Square badgeSquare = NoSquare;
    MoveQuality badgeQuality = MoveQuality::Best;
    // Vorgemerkter Zug (Premove): beide Felder blau getoent
    std::optional<Move> premove;
    // Gesuchter Zug im Training: richtig (gruen) oder falsch (rot) kurz aufleuchten lassen
    std::optional<Move> flashMove;
    bool flashGood = true;
};

// Eigene Markierung des Spielers (Rechtsklick): Pfeil von -> nach, oder Kreis (to == NoSquare).
struct Annotation {
    Square from = NoSquare;
    Square to = NoSquare;
    int color = 0;  // 0 gruen, 1 rot (Shift), 2 blau (Strg), 3 gelb (Alt)

    bool isCircle() const { return to == NoSquare; }
};

// Zeichnet ein Brett beliebiger Groesse (W x H) und nimmt Zuege per Klick oder Ziehen entgegen.
// Rechtsklick + Ziehen zeichnet Pfeile, Rechtsklick auf ein Feld einen Kreis.
class BoardView {
public:
    // Belegt das Rechteck [origin, origin + avail] (Brett wird darin zentriert).
    // Gibt einen Zug zurueck, sobald der Spieler einen vollstaendigen, legalen Zug gemacht hat.
    std::optional<Move> draw(const Position& pos, const std::vector<Move>& legal, bool interactive, bool flipped,
                             const BoardOverlay& overlay, const Theme& theme, ImVec2 origin, ImVec2 avail);

    void cancel();

    // Figur gleitet von from nach to (ab jetzt, ~0,16 s). Ohne Aufruf springen Figuren wie bisher.
    void animate(const Move& m) { anim_ = m; animStart_ = ImGui::GetTime(); }
    bool animating() const { return anim_.has_value(); }

    const std::vector<Annotation>& annotations() const { return annotations_; }
    void clearAnnotations() { annotations_.clear(); }
    void addAnnotation(const Annotation& a) { toggleAnnotation(a); }
    void select(Square s) { selected_ = s; }
    static ImVec4 annotationColor(int color);

    // Rechteck des zuletzt gezeichneten Bretts (fuer Overlays darueber).
    ImVec2 boardMin() const { return min_; }
    ImVec2 boardMax() const { return max_; }

    // Einzelne Figur zeichnen (auch fuer Umwandlungsauswahl und Panels).
    static void drawPiece(ImDrawList* dl, const Theme& theme, const PieceType& type, Color color, ImVec2 center,
                          float cell);

private:
    ImVec2 squareMin(const Position& pos, Square s) const;
    ImVec2 squareCenter(const Position& pos, Square s) const;
    Square squareAt(const Position& pos, ImVec2 p) const;
    std::vector<Move> movesBetween(const std::vector<Move>& legal, Square from, Square to) const;
    bool hasMovesFrom(const std::vector<Move>& legal, Square from) const;
    void toggleAnnotation(const Annotation& a);

    ImVec2 min_{}, max_{};
    float cell_ = 0;
    bool flipped_ = false;
    int lastWidth_ = 0, lastHeight_ = 0;

    Square selected_ = NoSquare;
    Square dragFrom_ = NoSquare;
    bool dragging_ = false;
    std::vector<Move> promotionChoices_;

    std::vector<Annotation> annotations_;
    std::optional<Move> anim_;
    double animStart_ = 0;
    Square rightFrom_ = NoSquare;
    bool rightDragging_ = false;
};

}  // namespace chess::ui
