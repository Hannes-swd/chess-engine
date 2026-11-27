#include "ui/board_view.hpp"
#include "ui/i18n.hpp"

#include "core/tactics.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <string>

namespace chess::ui {

namespace {

// Gefuellte Schachsymbole fuer beide Farben; die Farbe kommt aus Fuellung und Kontur.
const char* glyphFor(const std::string& id) {
    if (id == "king") return "\xE2\x99\x9A";    // U+265A
    if (id == "queen") return "\xE2\x99\x9B";   // U+265B
    if (id == "rook") return "\xE2\x99\x9C";    // U+265C
    if (id == "bishop") return "\xE2\x99\x9D";  // U+265D
    if (id == "knight") return "\xE2\x99\x9E";  // U+265E
    if (id == "pawn") return "\xE2\x99\x9F";    // U+265F
    return nullptr;
}

void drawArrow(ImDrawList* dl, ImVec2 a, ImVec2 b, float cell, ImU32 col) {
    const ImVec2 d = b - a;
    const float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 1.0f) return;
    const ImVec2 n = d / len;
    const ImVec2 perp(-n.y, n.x);
    const float head = cell * 0.36f;
    const float width = cell * 0.14f;
    const ImVec2 start = a + n * (cell * 0.18f);
    const ImVec2 neck = b - n * head;
    dl->AddLine(start, neck, col, width);
    dl->AddTriangleFilled(b, neck + perp * (head * 0.62f), neck - perp * (head * 0.62f), col);
}

ImVec4 hex(unsigned rgb, float a) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

}  // namespace

ImVec4 BoardView::annotationColor(int color) {
    switch (color) {
        case 1: return hex(0xD0605A, 0.80f);  // rot
        case 2: return hex(0x5A8FD0, 0.80f);  // blau
        case 3: return hex(0xD8B84A, 0.85f);  // gelb
        default: return hex(0x5FAE72, 0.80f);  // gruen
    }
}

void BoardView::cancel() {
    selected_ = NoSquare;
    dragFrom_ = NoSquare;
    dragging_ = false;
    promotionChoices_.clear();
}

void BoardView::toggleAnnotation(const Annotation& a) {
    for (auto it = annotations_.begin(); it != annotations_.end(); ++it) {
        if (it->from != a.from || it->to != a.to) continue;
        if (it->color == a.color)
            annotations_.erase(it);  // gleiche Markierung nochmal -> weg
        else
            it->color = a.color;  // andere Farbe -> umfaerben
        return;
    }
    annotations_.push_back(a);
}

ImVec2 BoardView::squareMin(const Position& pos, Square s) const {
    const int f = pos.fileOf(s);
    const int r = pos.rankOf(s);
    const int col = flipped_ ? pos.width() - 1 - f : f;
    const int row = flipped_ ? r : pos.height() - 1 - r;
    return ImVec2(min_.x + col * cell_, min_.y + row * cell_);
}

ImVec2 BoardView::squareCenter(const Position& pos, Square s) const {
    return squareMin(pos, s) + ImVec2(cell_ * 0.5f, cell_ * 0.5f);
}

Square BoardView::squareAt(const Position& pos, ImVec2 p) const {
    if (p.x < min_.x || p.y < min_.y || p.x >= max_.x || p.y >= max_.y) return NoSquare;
    const int col = static_cast<int>((p.x - min_.x) / cell_);
    const int row = static_cast<int>((p.y - min_.y) / cell_);
    const int f = flipped_ ? pos.width() - 1 - col : col;
    const int r = flipped_ ? row : pos.height() - 1 - row;
    return pos.contains(f, r) ? pos.square(f, r) : NoSquare;
}

std::vector<Move> BoardView::movesBetween(const std::vector<Move>& legal, Square from, Square to) const {
    std::vector<Move> out;
    for (const Move& m : legal)
        if (m.from == from && m.to == to) out.push_back(m);
    return out;
}

bool BoardView::hasMovesFrom(const std::vector<Move>& legal, Square from) const {
    return std::any_of(legal.begin(), legal.end(), [&](const Move& m) { return m.from == from; });
}

void BoardView::drawPiece(ImDrawList* dl, const Theme& theme, const PieceType& type, Color color, ImVec2 center,
                          float cell) {
    const Palette& c = theme.p();
    const ImU32 fill = Theme::u32(color == Color::White ? c.whiteFill : c.blackFill);
    const ImU32 outline = Theme::u32(color == Color::White ? c.whiteOutline : c.blackOutline);
    // Eigenes Bild (z. B. aus einem Plugin) hat Vorrang
    if (theme.pieceImage) {
        const std::string& path = color == Color::White ? (type.imageWhite.empty() ? type.imageBlack : type.imageWhite)
                                                        : (type.imageBlack.empty() ? type.imageWhite : type.imageBlack);
        if (!path.empty())
            if (const ImTextureID tex = theme.pieceImage(path)) {
                const float h = cell * 0.46f;
                dl->AddImage(tex, ImVec2(center.x - h, center.y - h), ImVec2(center.x + h, center.y + h));
                return;
            }
    }
    const char* glyph = glyphFor(type.id);
    // Figurensatz "Buchstaben": deutsche Kuerzel auf Scheiben (K, D, T, L, S, B) – hilft beim Notation-Lernen
    if (theme.pieceSet == 2) {
        static const std::pair<const char*, const char*> german[] = {{"king", "K"},   {"queen", "D"},  {"rook", "T"},
                                                                     {"bishop", "L"}, {"knight", "S"}, {"pawn", "B"}};
        std::string letter(1, type.symbol);
        for (const auto& [id, l] : german)
            if (type.id == id) letter = l;
        const float r = type.pawnLike ? cell * 0.30f : cell * 0.38f;
        dl->AddCircleFilled(center + ImVec2(0, cell * 0.03f), r, IM_COL32(0, 0, 0, 50));
        dl->AddCircleFilled(center, r, fill);
        dl->AddCircle(center, r, outline, 0, std::max(1.5f, cell * 0.025f));
        ImFont* font = theme.heading ? theme.heading : ImGui::GetFont();
        const float fs = type.pawnLike ? cell * 0.34f : cell * 0.44f;
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, letter.c_str());
        dl->AddText(font, fs, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), outline, letter.c_str());
        return;
    }
    if (glyph && theme.pieces) {
        const float fs = cell * 0.84f;
        if (theme.pieceSet == 1) {
            // Figurensatz "Diagramm": wie in Schachbuechern – Flaeche plus feine Linienzeichnung
            // (Umriss-Symbole U+2654..2659 ueber der gefuellten Form)
            static const std::pair<const char*, const char*> lineArt[] = {
                {"king", "\xE2\x99\x94"},   {"queen", "\xE2\x99\x95"},  {"rook", "\xE2\x99\x96"},
                {"bishop", "\xE2\x99\x97"}, {"knight", "\xE2\x99\x98"}, {"pawn", "\xE2\x99\x99"}};
            const char* lines = nullptr;
            for (const auto& [id, g] : lineArt)
                if (type.id == id) lines = g;
            const ImVec2 ts = theme.pieces->CalcTextSizeA(fs, FLT_MAX, 0.0f, glyph);
            const ImVec2 pos(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f);
            const bool white = color == Color::White;
            dl->AddText(theme.pieces, fs, pos, Theme::u32(white ? c.whiteFill : c.blackFill), glyph);
            if (lines) {
                const ImVec2 ls = theme.pieces->CalcTextSizeA(fs, FLT_MAX, 0.0f, lines);
                const ImVec2 lp(center.x - ls.x * 0.5f, center.y - ls.y * 0.5f);
                dl->AddText(theme.pieces, fs, lp, Theme::u32(white ? c.whiteOutline : c.blackOutline), lines);
            }
            return;
        }
        const ImVec2 ts = theme.pieces->CalcTextSizeA(fs, FLT_MAX, 0.0f, glyph);
        const ImVec2 pos(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f);
        const float o = std::max(1.0f, cell * 0.022f);
        // weicher Schatten, Kontur in 8 Richtungen, dann Fuellung
        dl->AddText(theme.pieces, fs, pos + ImVec2(0, cell * 0.035f), IM_COL32(0, 0, 0, 55), glyph);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx || dy) dl->AddText(theme.pieces, fs, pos + ImVec2(dx * o, dy * o), outline, glyph);
        dl->AddText(theme.pieces, fs, pos, fill, glyph);
        return;
    }

    // Plugin-Figuren (oder kein Symbol-Font): Scheibe mit Buchstaben
    const float r = cell * 0.36f;
    dl->AddCircleFilled(center + ImVec2(0, cell * 0.03f), r, IM_COL32(0, 0, 0, 55));
    dl->AddCircleFilled(center, r, fill);
    dl->AddCircle(center, r, outline, 0, std::max(1.5f, cell * 0.03f));
    const char sym[2] = {type.symbol, 0};
    ImFont* font = theme.heading ? theme.heading : ImGui::GetFont();
    const float fs = cell * 0.42f;
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, sym);
    dl->AddText(font, fs, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), outline, sym);
}

std::optional<Move> BoardView::draw(const Position& pos, const std::vector<Move>& legal, bool interactive,
                                    bool flipped, const BoardOverlay& ov, const Theme& theme, ImVec2 origin,
                                    ImVec2 avail) {
    std::optional<Move> result;
    const Palette& c = theme.p();
    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    flipped_ = flipped;
    cell_ = std::floor(std::min(avail.x / pos.width(), avail.y / pos.height()));
    if (cell_ < 8) return result;
    const ImVec2 size(cell_ * pos.width(), cell_ * pos.height());
    min_ = ImVec2(std::floor(origin.x + (avail.x - size.x) * 0.5f), std::floor(origin.y + (avail.y - size.y) * 0.5f));
    max_ = min_ + size;

    // Andere Brettgroesse (Variante gewechselt): alte Markierungen passen nicht mehr
    if (pos.width() != lastWidth_ || pos.height() != lastHeight_) {
        if (lastWidth_ != 0) annotations_.clear();  // beim allerersten Zeichnen nichts loeschen
        lastWidth_ = pos.width();
        lastHeight_ = pos.height();
    }

    if (!interactive) {
        selected_ = NoSquare;
        dragging_ = false;
    }
    if (selected_ != NoSquare && (selected_ >= pos.squareCount() || !hasMovesFrom(legal, selected_))) cancel();

    // --- Eingabe ---------------------------------------------------------------
    ImGui::SetCursorScreenPos(min_);
    ImGui::SetNextItemAllowOverlap();  // Ergebnis-Karte ueber dem Brett muss klickbar bleiben
    ImGui::InvisibleButton("##board", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool boardHovered = ImGui::IsItemHovered();
    const ImVec2 mouse = io.MousePos;
    const Square hover = boardHovered ? squareAt(pos, mouse) : NoSquare;
    auto commit = [&](const std::vector<Move>& moves) {
        if (moves.size() == 1) {
            result = moves.front();
            cancel();
        } else {
            promotionChoices_ = moves;
            ImGui::OpenPopup("##promotion");
        }
    };

    // Linksklick: Markierungen weg (wie auf Lichess), dann Zug-Logik
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) annotations_.clear();
    if (interactive && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        const Square sq = squareAt(pos, mouse);
        bool done = false;
        if (selected_ != NoSquare && sq != NoSquare && sq != selected_) {
            const auto moves = movesBetween(legal, selected_, sq);
            if (!moves.empty()) {
                commit(moves);
                done = true;
            }
        }
        if (!done) {
            const Piece& p = sq == NoSquare ? Piece{} : pos.at(sq);
            if (!p.empty() && p.color == pos.sideToMove() && hasMovesFrom(legal, sq)) {
                selected_ = sq;
                dragFrom_ = sq;
                dragging_ = true;
            } else {
                cancel();
            }
        }
    }

    const bool dragVisible = dragging_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f);
    if (dragging_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        dragging_ = false;
        const Square sq = squareAt(pos, mouse);
        if (sq != NoSquare && sq != dragFrom_) {
            const auto moves = movesBetween(legal, dragFrom_, sq);
            if (!moves.empty()) commit(moves);
        }
    }

    // Rechtsklick: Auswahl aufheben und Pfeil/Kreis beginnen (geht immer, auch beim Nachspielen)
    if (!ov.allowAnnotations) {
        annotations_.clear();
        rightDragging_ = false;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        cancel();
        rightFrom_ = squareAt(pos, mouse);
        rightDragging_ = ov.allowAnnotations && rightFrom_ != NoSquare;
    }
    if (rightDragging_ && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        rightDragging_ = false;
        const Square to = squareAt(pos, mouse);
        const int color = io.KeyShift ? 1 : io.KeyCtrl ? 2 : io.KeyAlt ? 3 : 0;
        if (to == NoSquare || to == rightFrom_)
            toggleAnnotation(Annotation{rightFrom_, NoSquare, color});
        else
            toggleAnnotation(Annotation{rightFrom_, to, color});
    }

    // Hand-Cursor ueber allem, was man anfassen kann
    if (interactive && hover != NoSquare) {
        const Piece& hp = pos.at(hover);
        const bool movable = !hp.empty() && hp.color == pos.sideToMove() && hasMovesFrom(legal, hover);
        const bool target = selected_ != NoSquare && !movesBetween(legal, selected_, hover).empty();
        if (movable || target || dragVisible) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    // --- Felder ----------------------------------------------------------------
    for (Square s = 0; s < pos.squareCount(); ++s) {
        const ImVec2 a = squareMin(pos, s);
        const bool light = (pos.fileOf(s) + pos.rankOf(s)) % 2 == 1;
        dl->AddRectFilled(a, a + ImVec2(cell_, cell_), Theme::u32(light ? c.boardLight : c.boardDark));
    }

    auto tint = [&](Square s, const ImVec4& col) {
        const ImVec2 a = squareMin(pos, s);
        dl->AddRectFilled(a, a + ImVec2(cell_, cell_), Theme::u32(col));
    };
    if (ov.lastMove && ov.lastMove->valid()) {
        tint(ov.lastMove->from, c.lastMove);
        tint(ov.lastMove->to, c.lastMove);
    }
    if (selected_ != NoSquare) tint(selected_, c.selected);

    // Haengende Figuren: angegriffen und von keiner eigenen Figur gedeckt
    if (ov.hangingFor) {
        const Color own = *ov.hangingFor;
        const float inset = std::max(2.0f, cell_ * 0.05f);
        for (Square s = 0; s < pos.squareCount(); ++s) {
            const Piece& p = pos.at(s);
            if (p.empty() || p.color != own || pos.registry().get(p.type).royal) continue;
            if (!isEnPrise(pos, s)) continue;
            const ImVec2 a = squareMin(pos, s) + ImVec2(inset, inset);
            const ImVec2 b = squareMin(pos, s) + ImVec2(cell_ - inset, cell_ - inset);
            dl->AddRectFilled(a, b, Theme::u32(c.check, 0.16f), cell_ * 0.12f);
            dl->AddRect(a, b, Theme::u32(c.check, 0.85f), cell_ * 0.12f, 0, std::max(2.0f, cell_ * 0.04f));
        }
    }

    // Koenig im Schach: weicher roter Schein
    if (ov.showCheck && pos.inCheck(pos.sideToMove()))
        for (Square k : pos.royalSquares(pos.sideToMove())) {
            const ImVec2 ctr = squareCenter(pos, k);
            for (int i = 4; i >= 1; --i)
                dl->AddCircleFilled(ctr, cell_ * (0.18f + 0.08f * i), Theme::u32(c.check, 0.16f * (5 - i)));
        }

    // Koordinaten am Rand
    if (ov.showCoordinates) {
        ImFont* f = theme.body ? theme.body : ImGui::GetFont();
        const float fs = std::max(10.0f, cell_ * 0.17f);
        for (int i = 0; i < pos.width(); ++i) {
            const int file = flipped_ ? pos.width() - 1 - i : i;
            const Square s = pos.square(file, flipped_ ? pos.height() - 1 : 0);
            const bool light = (pos.fileOf(s) + pos.rankOf(s)) % 2 == 1;
            const char label[2] = {static_cast<char>('a' + file), 0};
            const ImVec2 a = squareMin(pos, s);
            dl->AddText(f, fs, ImVec2(a.x + cell_ - fs * 0.62f, a.y + cell_ - fs * 1.12f),
                        Theme::u32(light ? c.coordOnLight : c.coordOnDark), label);
        }
        for (int i = 0; i < pos.height(); ++i) {
            const Square s = pos.square(flipped_ ? pos.width() - 1 : 0, i);
            const bool light = (pos.fileOf(s) + pos.rankOf(s)) % 2 == 1;
            const std::string label = std::to_string(i + 1);
            const ImVec2 a = squareMin(pos, s);
            dl->AddText(f, fs, ImVec2(a.x + fs * 0.25f, a.y + fs * 0.12f),
                        Theme::u32(light ? c.coordOnLight : c.coordOnDark), label.c_str());
        }
    }

    // Vorgemerkter Zug (blau) und Trainings-Rueckmeldung (gruen/rot)
    if (ov.premove && ov.premove->valid() && ov.premove->to < pos.squareCount()) {
        tint(ov.premove->from, BoardView::annotationColor(2) * ImVec4(1, 1, 1, 0.45f));
        tint(ov.premove->to, BoardView::annotationColor(2) * ImVec4(1, 1, 1, 0.45f));
    }
    if (ov.flashMove && ov.flashMove->valid() && ov.flashMove->to < pos.squareCount()) {
        const ImVec4 col = ov.flashGood ? c.qBest : c.qBlunder;
        tint(ov.flashMove->from, ImVec4(col.x, col.y, col.z, 0.35f));
        tint(ov.flashMove->to, ImVec4(col.x, col.y, col.z, 0.5f));
    }

    // --- Figuren -----------------------------------------------------------------
    // Animation: die gezogene Figur (und bei der Rochade der Turm) gleitet zum Zielfeld
    constexpr double kAnimSeconds = 0.16;
    float animT = 1.0f;
    if (anim_) {
        const double t = (ImGui::GetTime() - animStart_) / kAnimSeconds;
        const bool fits = anim_->from < pos.squareCount() && anim_->to < pos.squareCount() && !pos.at(anim_->to).empty();
        if (t >= 1.0 || !fits || dragVisible)
            anim_.reset();
        else
            animT = static_cast<float>(1.0 - (1.0 - t) * (1.0 - t));  // abbremsen
    }
    auto animated = [&](Square s) {
        return anim_ && (s == anim_->to || (anim_->rookTo != NoSquare && s == anim_->rookTo));
    };
    for (Square s = 0; s < pos.squareCount(); ++s) {
        const Piece& p = pos.at(s);
        if (p.empty() || (dragVisible && s == dragFrom_) || animated(s)) continue;
        drawPiece(dl, theme, pos.registry().get(p.type), p.color, squareCenter(pos, s), cell_);
    }
    if (anim_) {
        auto glide = [&](Square from, Square to) {
            if (from == NoSquare || to == NoSquare || pos.at(to).empty()) return;
            const ImVec2 a = squareCenter(pos, from), b = squareCenter(pos, to);
            const Piece& p = pos.at(to);
            drawPiece(dl, theme, pos.registry().get(p.type), p.color, a + (b - a) * animT, cell_);
        };
        if (anim_->rookTo != NoSquare) glide(anim_->rookFrom, anim_->rookTo);
        glide(anim_->from, anim_->to);
    }

    // Wohin kann die ausgewaehlte Figur? Punkte fuer leere Felder, Ringe fuer Schlagzuege,
    // rot, wenn der Gegner das Zielfeld nach dem Zug angreift.
    if (selected_ != NoSquare && ov.showHints) {
        Position probe = pos;
        const Color mover = pos.at(selected_).color;
        std::vector<Square> drawn;
        for (const Move& m : legal) {
            if (m.from != selected_) continue;
            if (std::find(drawn.begin(), drawn.end(), m.to) != drawn.end()) continue;  // Umwandlungen nur einmal
            drawn.push_back(m.to);

            bool danger = false;
            if (ov.showDanger && !pos.registry().get(pos.at(m.from).type).royal) {
                const auto u = probe.makeMove(m);
                danger = isEnPrise(probe, m.to);
                probe.unmakeMove(m, u);
            }
            (void)mover;
            const ImU32 col = danger ? Theme::u32(c.check, 0.55f) : Theme::u32(c.hint);
            const ImVec2 ctr = squareCenter(pos, m.to);
            if (m.flags & FlagCapture)
                dl->AddCircle(ctr, cell_ * 0.43f, col, 0, cell_ * 0.075f);
            else
                dl->AddCircleFilled(ctr, cell_ * 0.15f, col);
        }
    }

    // Rahmen um das Zielfeld unter der Maus (beim Ziehen und bei Auswahl)
    const Square fromSq = dragVisible ? dragFrom_ : selected_;
    if (ov.showTargetFrame && fromSq != NoSquare && hover != NoSquare && !movesBetween(legal, fromSq, hover).empty()) {
        const ImVec2 a = squareMin(pos, hover);
        dl->AddRect(a, a + ImVec2(cell_, cell_), Theme::u32(c.accent), 0, 0, std::max(2.0f, cell_ * 0.05f));
    }

    // Eigene Markierungen (Rechtsklick)
    for (const Annotation& a : annotations_) {
        const ImU32 col = Theme::u32(annotationColor(a.color));
        if (a.isCircle())
            dl->AddCircle(squareCenter(pos, a.from), cell_ * 0.45f, col, 0, cell_ * 0.07f);
        else
            drawArrow(dl, squareCenter(pos, a.from), squareCenter(pos, a.to), cell_, col);
    }
    if (rightDragging_ && rightFrom_ != NoSquare) {
        const int color = io.KeyShift ? 1 : io.KeyCtrl ? 2 : io.KeyAlt ? 3 : 0;
        ImVec4 preview = annotationColor(color);
        preview.w *= 0.55f;
        if (hover != NoSquare && hover != rightFrom_)
            drawArrow(dl, squareCenter(pos, rightFrom_), squareCenter(pos, hover), cell_, Theme::u32(preview));
        else
            dl->AddCircle(squareCenter(pos, rightFrom_), cell_ * 0.45f, Theme::u32(preview), 0, cell_ * 0.07f);
    }

    // Pfeile aus Analyse/Tipp
    for (const BoardArrow& a : ov.arrows)
        if (a.move.valid())
            drawArrow(dl, squareCenter(pos, a.move.from), squareCenter(pos, a.move.to), cell_,
                      a.color ? a.color : Theme::u32(c.arrow));

    // Bewertungs-Abzeichen rechts oben auf dem Zielfeld
    if (ov.badgeSquare != NoSquare && ov.badgeSquare < pos.squareCount()) {
        const ImVec2 a = squareMin(pos, ov.badgeSquare);
        theme.qualityBadge(dl, ov.badgeQuality, ImVec2(a.x + cell_ * 0.84f, a.y + cell_ * 0.16f), cell_ * 0.17f);
    }

    // Gezogene Figur folgt der Maus (ueber allem)
    if (dragVisible && dragFrom_ != NoSquare) {
        const Piece& p = pos.at(dragFrom_);
        if (!p.empty())
            drawPiece(ImGui::GetForegroundDrawList(), theme, pos.registry().get(p.type), p.color, mouse, cell_ * 1.08f);
    }

    // --- Umwandlung --------------------------------------------------------------
    if (!promotionChoices_.empty()) {
        const ImVec2 at = squareMin(pos, promotionChoices_.front().to);
        ImGui::SetNextWindowPos(ImVec2(at.x, at.y), ImGuiCond_Appearing);
    }
    if (ImGui::BeginPopup("##promotion")) {
        ImGui::TextDisabled(tr("Umwandeln in"));
        const Color mover = promotionChoices_.empty() ? pos.sideToMove() : pos.at(promotionChoices_.front().from).color;
        for (size_t i = 0; i < promotionChoices_.size(); ++i) {
            const Move m = promotionChoices_[i];
            const PieceType& t = pos.registry().get(m.promotion);
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 bsize(cell_ * 0.9f, cell_ * 0.9f);
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##promo", bsize)) {
                result = m;
                cancel();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.name.c_str());
            drawPiece(ImGui::GetWindowDrawList(), theme, t, mover, p0 + bsize * 0.5f, bsize.x);
            ImGui::PopID();
            if (i + 1 < promotionChoices_.size()) ImGui::SameLine();
        }
        ImGui::EndPopup();
    } else if (!promotionChoices_.empty() && !ImGui::IsPopupOpen("##promotion")) {
        promotionChoices_.clear();  // Auswahl abgebrochen (Klick daneben)
    }

    return result;
}

}  // namespace chess::ui
