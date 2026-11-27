// Analyse-Bericht: Bewertungskurve, Genauigkeit, Vorteilsverteilung, Zugkategorien und
// ein Coach, der jeden Zug erklaert und die bessere Fortsetzung auf dem Brett vorspielt.

#include "ui/gui_app.hpp"

#include "core/notation.hpp"
#include "ui/icons.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chess::ui {

namespace {

std::string decimal(double v, int digits = 1) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", digits, v);
    std::string s = buf;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

ImVec4 alpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

constexpr MoveQuality kOrder[] = {MoveQuality::Brilliant,  MoveQuality::Great,   MoveQuality::Best,
                                  MoveQuality::Excellent,  MoveQuality::Good,    MoveQuality::Inaccuracy,
                                  MoveQuality::Mistake,    MoveQuality::Miss,    MoveQuality::Blunder,
                                  MoveQuality::Forced};

}  // namespace

// ===========================================================================
// Variante ansehen
// ===========================================================================

void GuiApp::startPreview(const Position& base, std::vector<Move> moves, std::string title) {
    if (moves.empty()) return;
    preview_.emplace(Preview{base, base, std::move(moves), 0, std::move(title)});
    setPreviewIndex(1);  // ersten Zug gleich zeigen
    board_.cancel();
}

void GuiApp::setPreviewIndex(int index) {
    if (!preview_) return;
    Preview& p = *preview_;
    index = std::clamp(index, 0, static_cast<int>(p.moves.size()));
    p.current = p.base;
    for (int i = 0; i < index; ++i) {
        const auto legal = p.current.legalMoves();
        if (std::find(legal.begin(), legal.end(), p.moves[static_cast<size_t>(i)]) == legal.end()) {
            index = i;
            break;
        }
        p.current.makeMove(p.moves[static_cast<size_t>(i)]);
    }
    p.index = index;
}

void GuiApp::drawPreviewBanner(ImVec2 min, ImVec2 max) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    Preview& p = *preview_;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(min, max, Theme::u32(c.accent, 0.16f), 8 * s);
    dl->AddRect(min, max, Theme::u32(c.accent, 0.55f), 8 * s);

    const float btn = 22.0f * s + 2 * ImGui::GetStyle().FramePadding.y;
    const float y = (min.y + max.y - btn) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(max.x - 3 * btn - 12 * s, y));
    if (iconButton("##pvprev", icon::prev, "Zug zurück (←)", false, 0, p.index > 0)) setPreviewIndex(p.index - 1);
    ImGui::SameLine(0, 2 * s);
    if (iconButton("##pvnext", icon::next, "Zug vor (→)", false, 0, p.index < static_cast<int>(p.moves.size())))
        setPreviewIndex(p.index + 1);
    ImGui::SameLine(0, 2 * s);
    const bool close = iconButton("##pvclose", icon::close, "Zurück zur Partie (Esc)");

    ImFont* f = theme_.strong ? theme_.strong : ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const std::string label = p.title + "   " + std::to_string(p.index) + " / " + std::to_string(p.moves.size());
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, label.c_str());
    Theme::glyphCentered(dl, ImGui::GetFont(), 22 * s, ImVec2(min.x + 22 * s, (min.y + max.y) * 0.5f),
                         Theme::u32(c.accent), icon::replay);
    dl->AddText(f, fs, ImVec2(min.x + 40 * s, (min.y + max.y - ts.y) * 0.5f), Theme::u32(c.text), label.c_str());
    if (close) preview_.reset();
}

// ===========================================================================
// Bewertungskurve
// ===========================================================================

void GuiApp::drawEvalGraph(float height) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const GameAnalysis& a = *analysis_;
    const int points = static_cast<int>(a.whiteEvals.size());
    if (points < 2) return;

    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##graph", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + width, p0.y + height);

    auto xAt = [&](int k) { return p0.x + width * k / static_cast<float>(points - 1); };
    auto yAt = [&](int k) {
        return p0.y + height * (1.0f - static_cast<float>(winChance(a.whiteEvals[static_cast<size_t>(k)])) / 100.0f);
    };

    // Schwarz oben, Weiss waechst von unten: je hoeher die helle Flaeche, desto besser fuer Weiss.
    // Eine einzige Flaeche (statt vieler Vierecke), damit keine Naehte sichtbar sind.
    dl->AddRectFilled(p0, p1, Theme::u32(c.blackFill), 6 * s);
    std::vector<ImVec2> area;
    area.reserve(static_cast<size_t>(points) + 2);
    area.emplace_back(p0.x, p1.y);
    for (int k = 0; k < points; ++k) area.emplace_back(xAt(k), yAt(k));
    area.emplace_back(p1.x, p1.y);
    dl->AddConcavePolyFilled(area.data(), static_cast<int>(area.size()), Theme::u32(c.whiteFill, 0.92f));
    dl->AddPolyline(area.data() + 1, points, Theme::u32(c.textMuted, 0.8f), ImDrawFlags_None, 1.2f * s);
    dl->AddLine(ImVec2(p0.x, (p0.y + p1.y) * 0.5f), ImVec2(p1.x, (p0.y + p1.y) * 0.5f), Theme::u32(c.textMuted, 0.45f),
                1.0f * s);

    // Aktuelle Stellung
    const int current = std::clamp(viewPly(), 0, points - 1);
    dl->AddLine(ImVec2(xAt(current), p0.y), ImVec2(xAt(current), p1.y), Theme::u32(c.accent), 2.0f * s);

    // Markierungen: besonders gute und schlechte Zuege
    for (const MoveReview& r : a.moves) {
        const MoveQuality q = r.quality;
        if (!(isError(q) || q == MoveQuality::Brilliant || q == MoveQuality::Great)) continue;
        const int k = r.ply + 1;
        if (k >= points) continue;
        const ImVec2 ctr(xAt(k), yAt(k));
        dl->AddCircleFilled(ctr, 5.0f * s, Theme::u32(theme_.quality(q)));
        dl->AddCircle(ctr, 5.0f * s, Theme::u32(c.surface), 0, 1.5f * s);
    }
    dl->AddRect(p0, p1, Theme::u32(c.border), 6 * s);

    // Hover: naechste Stellung finden, Tooltip, Klick springt hin
    if (hovered) {
        const float mx = ImGui::GetIO().MousePos.x;
        const int k = std::clamp(static_cast<int>(std::lround((mx - p0.x) / width * (points - 1))), 0, points - 1);
        dl->AddLine(ImVec2(xAt(k), p0.y), ImVec2(xAt(k), p1.y), Theme::u32(c.textMuted, 0.7f), 1.0f * s);
        std::string tip = k == 0 ? std::string("Startstellung") : figurine(moveLabel(k - 1));
        tip += "\n" + a.whiteEvals[static_cast<size_t>(k)].toString();
        if (k > 0 && k - 1 < static_cast<int>(a.moves.size()))
            tip += "   " + qualityName(a.moves[static_cast<size_t>(k - 1)].quality);
        ImGui::SetTooltip("%s", tip.c_str());
        if (clicked) {
            preview_.reset();
            setView(k);
        }
    }
}

// ===========================================================================
// Bericht
// ===========================================================================

void GuiApp::tabAnalysis() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const int n = game_.ply();
    ImGui::BeginChild("##ana", ImVec2(0, 0), ImGuiChildFlags_None);

    if (n == 0) {
        ImGui::Dummy(ImVec2(0, 16 * s));
        Theme::glyphCentered(ImGui::GetWindowDrawList(), ImGui::GetFont(), 40 * s,
                             ImGui::GetCursorScreenPos() + ImVec2(20 * s, 20 * s), Theme::u32(c.textMuted), icon::chart);
        ImGui::Dummy(ImVec2(0, 48 * s));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("Spiel eine Partie. Danach siehst du hier jeden Zug bewertet und erklärt.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        return;
    }

    auto runControls = [&]() {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##depth", &analysisDepth_, 4, 24, "Suchtiefe %d");
        ImGui::BeginDisabled(engineBusy());
        ImGui::PushFont(theme_.strong, 0.0f);
        if (primaryButton((std::string(icon::analysis) + "  " + (analysis_ ? "Neu analysieren" : "Partie analysieren")).c_str(),
                          ImVec2(-FLT_MIN, 42 * s)))
            startAnalysis();
        ImGui::PopFont();
        ImGui::EndDisabled();
    };

    if (analysisRunning()) {
        const int done = progressDone_->load(), total = std::max(1, progressTotal_->load());
        ImGui::TextUnformatted("Stockfish bewertet jede Stellung …");
        const std::string label = "Stellung " + std::to_string(done) + " von " + std::to_string(total);
        ImGui::ProgressBar(static_cast<float>(done) / total, ImVec2(-FLT_MIN, 0), label.c_str());
        if (ImGui::Button((std::string(icon::stop) + "  Abbrechen").c_str())) analysisStop_->store(true);
        if (!analysis_) {
            ImGui::EndChild();
            return;
        }
    } else if (!analysis_) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(
            "Jeder Zug wird mit dem besten Zug verglichen, eingestuft und erklärt – von brillant bis Patzer. "
            "Du siehst, wer wann besser stand, und kannst die bessere Fortsetzung auf dem Brett durchspielen.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 6 * s));
        runControls();
        ImGui::EndChild();
        return;
    }

    const GameAnalysis& a = *analysis_;
    if (analysisPly_ < n)
        ImGui::TextColored(c.qInaccuracy, "%d neue Züge seit der Analyse", n - analysisPly_);

    // --- Bewertungskurve --------------------------------------------------------
    drawEvalGraph(118 * s);
    ImGui::Dummy(ImVec2(0, 4 * s));

    // --- Genauigkeit ------------------------------------------------------------
    const float half = ImGui::GetContentRegionAvail().x * 0.5f;
    const ImVec2 accPos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < 2; ++i) {
        const Color side = i == 0 ? Color::White : Color::Black;
        const PlayerSummary& ps = a.summary(side);
        const float x = accPos.x + i * half;
        const ImVec2 disc(x + 8 * s, accPos.y + 12 * s);
        dl->AddCircleFilled(disc, 7 * s, Theme::u32(side == Color::White ? c.whiteFill : c.blackFill));
        dl->AddCircle(disc, 7 * s, Theme::u32(c.border), 0, 1.0f * s);
        dl->AddText(theme_.strong, ImGui::GetFontSize(), ImVec2(x + 22 * s, accPos.y + 1 * s), Theme::u32(c.text),
                    playerName(side).c_str());
        const std::string acc = decimal(ps.accuracy);
        dl->AddText(theme_.heading, 38 * s, ImVec2(x, accPos.y + 24 * s), Theme::u32(c.text), acc.c_str());
        const float accW = theme_.heading->CalcTextSizeA(38 * s, FLT_MAX, 0.0f, acc.c_str()).x;
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x + accW + 4 * s, accPos.y + 42 * s),
                    Theme::u32(c.textMuted), "Genauigkeit");
    }
    ImGui::Dummy(ImVec2(0, 76 * s));

    // --- Wer stand wie lange besser? ---------------------------------------------
    {
        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 b0 = ImGui::GetCursorScreenPos();
        const float bh = 10 * s;
        const float ww = w * static_cast<float>(a.whiteBetter / 100.0);
        const float eq = w * static_cast<float>(a.balanced / 100.0);
        dl->AddRectFilled(b0, ImVec2(b0.x + w, b0.y + bh), Theme::u32(c.blackFill), 5 * s);
        dl->AddRectFilled(b0, ImVec2(b0.x + ww, b0.y + bh), Theme::u32(c.whiteFill), 5 * s);
        dl->AddRectFilled(ImVec2(b0.x + ww, b0.y), ImVec2(b0.x + ww + eq, b0.y + bh), Theme::u32(c.textMuted, 0.6f));
        dl->AddRect(b0, ImVec2(b0.x + w, b0.y + bh), Theme::u32(c.border), 5 * s);
        ImGui::Dummy(ImVec2(w, bh + 4 * s));
        // Drei Werte nebeneinander: Zahl gross, Bedeutung klein darunter
        const std::string vals[3] = {decimal(a.whiteBetter, 0) + " %", decimal(a.balanced, 0) + " %",
                                     decimal(a.blackBetter, 0) + " %"};
        const std::string labels[3] = {playerName(Color::White) + " vorn", "ausgeglichen",
                                       playerName(Color::Black) + " vorn"};
        const float y = ImGui::GetCursorScreenPos().y + 2 * s;
        ImFont* hf = theme_.heading ? theme_.heading : ImGui::GetFont();
        ImFont* f = ImGui::GetFont();
        for (int k = 0; k < 3; ++k) {
            const float vw = hf->CalcTextSizeA(19 * s, FLT_MAX, 0.0f, vals[k].c_str()).x;
            const float lw = f->CalcTextSizeA(14 * s, FLT_MAX, 0.0f, labels[k].c_str()).x;
            // links, Mitte, rechts buendig
            const float colW = std::max(vw, lw);
            const float x = k == 0 ? b0.x : k == 1 ? b0.x + (w - colW) * 0.5f : b0.x + w - colW;
            const float ax = k == 0 ? 0.0f : k == 1 ? 0.5f : 1.0f;
            dl->AddText(hf, 19 * s, ImVec2(x + (colW - vw) * ax, y), Theme::u32(c.text), vals[k].c_str());
            dl->PushClipRect(ImVec2(b0.x, y), ImVec2(b0.x + w, y + 60 * s), true);
            dl->AddText(f, 14 * s, ImVec2(x + (colW - lw) * ax, y + 22 * s), Theme::u32(c.textMuted), labels[k].c_str());
            dl->PopClipRect();
        }
        ImGui::Dummy(ImVec2(w, 44 * s));
    }
    ImGui::Dummy(ImVec2(0, 4 * s));

    // --- Zug fuer Zug: der Coach ------------------------------------------------
    const int idx = viewPly() - 1;
    if (const MoveReview* r = reviewFor(idx)) {
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float rad = 15 * s;
        theme_.qualityBadge(dl, r->quality, ImVec2(cp.x + rad, cp.y + rad), rad);
        ImGui::SetCursorScreenPos(ImVec2(cp.x + 2 * rad + 10 * s, cp.y + 2 * s));
        ImGui::PushFont(theme_.heading, 21.0f);
        // Zug im Titel mit Figurensymbol, wie in der Zugliste
        std::string headline = r->headline;
        if (const size_t at = headline.find(r->san); !r->san.empty() && at != std::string::npos)
            headline.replace(at, r->san.size(), figurine(r->san));
        ImGui::TextColored(theme_.quality(r->quality), "%s", headline.c_str());
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + 2 * rad + 8 * s));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        ImGui::TextUnformatted(r->explanation.c_str());
        ImGui::PopTextWrapPos();
        const Score white = r->mover == Color::White ? r->playedScore : r->playedScore.negated();
        ImGui::TextDisabled("%s   Bewertung danach %s   Genauigkeit %s %%", figurine(moveLabel(idx)).c_str(),
                            white.toString().c_str(), decimal(r->accuracy, 0).c_str());

        // Aktionen: Variante auf dem Brett durchspielen
        const bool wasBest = r->quality == MoveQuality::Best || r->quality == MoveQuality::Brilliant ||
                             r->quality == MoveQuality::Great || r->quality == MoveQuality::Forced;
        if (!wasBest && !r->bestLine.empty()) {
            if (primaryButton((std::string(icon::replay) + "  Besseren Zug zeigen").c_str()))
                startPreview(game_.positionAt(idx), r->bestLine, "Besser war " + figurine(r->bestSan));
            ImGui::SameLine();
        }
        if (!r->threatLine.empty()) {
            const char* label = wasBest ? "Fortsetzung zeigen" : "Antwort des Gegners";
            if (ImGui::Button((std::string(wasBest ? icon::forward : icon::threat) + "  " + label).c_str()))
                startPreview(game_.positionAt(idx + 1), r->threatLine,
                             wasBest ? "Fortsetzung nach " + figurine(r->san) : "Antwort auf " + figurine(r->san));
        }
    } else {
        ImGui::TextDisabled("Klick in die Kurve oder blättere mit ← → durch die Züge.");
    }

    // Navigation zwischen Zuegen und Fehlern
    {
        auto findError = [&](int from, int dir) {
            for (int j = from + dir; j >= 0 && j < static_cast<int>(a.moves.size()); j += dir)
                if (isError(a.moves[static_cast<size_t>(j)].quality)) return j;
            return -1;
        };
        const int prevErr = findError(idx, -1), nextErr = findError(idx, +1);
        ImGui::Dummy(ImVec2(0, 4 * s));
        const float w = ImGui::GetContentRegionAvail().x;
        const float bh = 22.0f * s + 2 * ImGui::GetStyle().FramePadding.y;
        const ImVec2 n0 = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(n0, ImVec2(n0.x + w, n0.y + bh), Theme::u32(c.surfaceRaised), 8 * s);
        const float w4 = w / 4;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (iconButton("##aprevErr", icon::skipPrev, "Vorheriger Fehler", false, w4, prevErr >= 0)) setView(prevErr + 1);
        ImGui::SameLine(0, 0);
        if (iconButton("##aprev", icon::prev, "Zug zurück (←)", false, w4, viewPly() > 0)) setView(viewPly() - 1);
        ImGui::SameLine(0, 0);
        if (iconButton("##anext", icon::next, "Zug vor (→)", false, w4, viewPly() < n)) setView(viewPly() + 1);
        ImGui::SameLine(0, 0);
        if (iconButton("##anextErr", icon::skipNext, "Nächster Fehler", false, w4, nextErr >= 0)) setView(nextErr + 1);
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, 6 * s));

    // --- Zugkategorien je Spieler ------------------------------------------------
    if (ImGui::BeginTable("##cats", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Kategorie", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("W", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("S", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        auto centered = [&](const char* text, ImVec4 col) {
            const float cw = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cw - ImGui::CalcTextSize(text).x) * 0.5f);
            ImGui::TextColored(col, "%s", text);
        };
        ImGui::TableNextRow();
        for (int col = 0; col < 2; ++col) {
            ImGui::TableSetColumnIndex(1 + col);
            const ImVec2 hp = ImGui::GetCursorScreenPos();
            const float cw = ImGui::GetContentRegionAvail().x;
            const ImVec2 disc(hp.x + cw * 0.5f, hp.y + ImGui::GetTextLineHeight() * 0.5f);
            dl->AddCircleFilled(disc, 7 * s, Theme::u32(col == 0 ? c.whiteFill : c.blackFill));
            dl->AddCircle(disc, 7 * s, Theme::u32(c.border), 0, 1.0f * s);
            ImGui::Dummy(ImVec2(cw, ImGui::GetTextLineHeight()));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", playerName(col == 0 ? Color::White : Color::Black).c_str());
        }
        for (MoveQuality q : kOrder) {
            const int w = a.white.counts[static_cast<size_t>(q)];
            const int b = a.black.counts[static_cast<size_t>(q)];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            const float rad = 9 * s;
            theme_.qualityBadge(dl, q, ImVec2(rp.x + rad, rp.y + ImGui::GetTextLineHeight() * 0.5f), rad);
            ImGui::SetCursorScreenPos(ImVec2(rp.x + 2 * rad + 8 * s, rp.y));
            if (w + b == 0)
                ImGui::TextDisabled("%s", qualityName(q).c_str());
            else
                ImGui::TextUnformatted(qualityName(q).c_str());
            for (int col = 0; col < 2; ++col) {
                ImGui::TableSetColumnIndex(1 + col);
                const int v = col == 0 ? w : b;
                const std::string num = std::to_string(v);
                centered(num.c_str(), v == 0 ? alpha(c.textMuted, 0.5f) : theme_.quality(q));
            }
        }
        ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0, 6 * s));
    sectionTitle("Neu analysieren");
    {
        const float btnW = ImGui::CalcTextSize("Starten").x + 52 * s;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW - ImGui::GetStyle().ItemSpacing.x);
        ImGui::SliderInt("##depth2", &analysisDepth_, 4, 24, "Suchtiefe %d");
        ImGui::SameLine();
        ImGui::BeginDisabled(engineBusy());
        if (ImGui::Button((std::string(icon::refresh) + "  Starten").c_str(), ImVec2(-FLT_MIN, 0))) startAnalysis();
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
}

}  // namespace chess::ui
