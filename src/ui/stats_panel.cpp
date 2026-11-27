// Bereich "Statistik": Elo-Verlauf, Bilanz (gesamt, mit Weiss/Schwarz, je Bot), Genauigkeit,
// Fehlerverteilung und die eigenen Eroeffnungen. Alles aus profil.json und den gespeicherten Partien.

#include "ui/gui_app.hpp"

#include "ui/icons.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace chess::ui {

namespace {

ImVec4 alpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

struct Tally {
    int w = 0, d = 0, l = 0;
    int games() const { return w + d + l; }
    double score() const { return games() ? (w + 0.5 * d) / games() * 100.0 : 0.0; }
};

}  // namespace

void GuiApp::drawEloCurve(float height) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const auto& h = player_.history;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + width, p0.y + height);
    ImGui::InvisibleButton("##elocurve", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, Theme::u32(alpha(c.surfaceRaised, 0.6f)), 6 * s);
    if (h.size() < 2) {
        const char* msg = "Die Kurve entsteht mit deinen gewerteten Partien.";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2((p0.x + p1.x - ts.x) * 0.5f, (p0.y + p1.y - ts.y) * 0.5f), Theme::u32(c.textMuted), msg);
        return;
    }
    int lo = h.front().rating, hi = lo;
    for (const auto& pt : h) lo = std::min(lo, pt.rating), hi = std::max(hi, pt.rating);
    lo -= 20;
    hi += 20;
    const float pad = 10 * s;
    auto xAt = [&](size_t i) { return p0.x + pad + (width - 2 * pad) * static_cast<float>(i) / (h.size() - 1); };
    auto yAt = [&](int r) { return p1.y - pad - (height - 2 * pad) * static_cast<float>(r - lo) / (hi - lo); };

    // Flaeche unter der Kurve, dann die Linie
    std::vector<ImVec2> line;
    for (size_t i = 0; i < h.size(); ++i) line.emplace_back(xAt(i), yAt(h[i].rating));
    std::vector<ImVec2> area = line;
    area.emplace_back(xAt(h.size() - 1), p1.y - pad);
    area.emplace_back(xAt(0), p1.y - pad);
    dl->AddConcavePolyFilled(area.data(), static_cast<int>(area.size()), Theme::u32(c.accent, 0.14f));
    dl->AddPolyline(line.data(), static_cast<int>(line.size()), Theme::u32(c.accent), ImDrawFlags_None, 2.0f * s);
    dl->AddCircleFilled(line.back(), 4 * s, Theme::u32(c.accent));

    // Hoechst- und Tiefstwert als ruhige Beschriftung
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", hi - 20);
    dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(p0.x + 6 * s, p0.y + 4 * s), Theme::u32(c.textMuted), buf);
    std::snprintf(buf, sizeof(buf), "%d", lo + 20);
    dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(p0.x + 6 * s, p1.y - 20 * s), Theme::u32(c.textMuted), buf);

    if (hovered) {
        const float mx = ImGui::GetIO().MousePos.x;
        size_t k = 0;
        for (size_t i = 0; i < h.size(); ++i)
            if (std::abs(xAt(i) - mx) < std::abs(xAt(k) - mx)) k = i;
        dl->AddCircle(line[k], 6 * s, Theme::u32(c.text), 0, 1.5f * s);
        std::string tip = "Elo " + std::to_string(h[k].rating);
        if (!h[k].opponent.empty()) tip += "\nGegen " + h[k].opponent;
        if (h[k].date.size() >= 10)
            tip += "\n" + h[k].date.substr(8, 2) + "." + h[k].date.substr(5, 2) + "." + h[k].date.substr(0, 4);
        ImGui::SetTooltip("%s", tip.c_str());
    }
}

void GuiApp::panelStats() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    if (ImGui::GetTime() - gamesScannedAt_ > 4.0) scanSavedGames();
    ImGui::BeginChild("##stats", ImVec2(0, 0), ImGuiChildFlags_None);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* hf = theme_.heading ? theme_.heading : ImGui::GetFont();

    // --- Elo ----------------------------------------------------------------------------
    {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float cw = ImGui::GetContentRegionAvail().x / 3;
        const std::string vals[3] = {std::to_string(player_.rating), std::to_string(player_.peak),
                                     std::to_string(player_.games)};
        const char* labels[3] = {"Elo", "Bestwert", "gewertete Partien"};
        for (int i = 0; i < 3; ++i) {
            dl->AddText(hf, 28 * s, ImVec2(p0.x + i * cw, p0.y), Theme::u32(i == 0 ? c.accent : c.text), vals[i].c_str());
            dl->AddText(ImGui::GetFont(), 14 * s, ImVec2(p0.x + i * cw, p0.y + 32 * s), Theme::u32(c.textMuted), labels[i]);
        }
        ImGui::Dummy(ImVec2(0, 54 * s));
    }
    drawEloCurve(120 * s);

    // --- Aus den gespeicherten Partien ------------------------------------------------------
    Tally all, asWhite, asBlack;
    std::map<std::string, Tally> byOpponent;
    std::map<std::string, Tally> byOpening;
    std::array<int, kQualityCount> counts{};
    std::vector<double> accuracies;  // aelteste zuerst
    for (auto it = savedGames_.rbegin(); it != savedGames_.rend(); ++it) {  // Liste ist neueste zuerst
        const SavedGame& g = *it;
        const bool white = g.white == "Du", black = g.black == "Du";
        if (!white && !black) continue;
        const int r = g.result == "1-0" ? (white ? 1 : -1) : g.result == "0-1" ? (white ? -1 : 1) : g.result == "1/2-1/2" ? 0 : 2;
        if (r == 2) continue;  // unbeendet
        auto add = [&](Tally& t) { (r > 0 ? t.w : r < 0 ? t.l : t.d)++; };
        add(all);
        add(white ? asWhite : asBlack);
        add(byOpponent[white ? g.black : g.white]);
        if (!g.opening.empty()) add(byOpening[g.opening]);
        for (size_t q = 0; q < counts.size(); ++q) counts[q] += g.countsYou[q];
        if (g.accuracyYou) accuracies.push_back(*g.accuracyYou);
    }

    sectionTitle("Bilanz");
    if (all.games() == 0) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(c.textMuted, "%s", "Noch keine beendeten Partien von dir gespeichert.");
        ImGui::PopTextWrapPos();
    } else {
        auto bar = [&](const char* label, const Tally& t) {
            const float fullW = ImGui::GetContentRegionAvail().x;
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            // Siege – Remis – Niederlagen, wie in Turniertabellen
            const std::string nums = std::to_string(t.w) + " – " + std::to_string(t.d) + " – " + std::to_string(t.l) +
                                     "   (" + std::to_string(static_cast<int>(std::lround(t.score()))) + " %)";
            dl->AddText(ImGui::GetFont(), 16 * s, p0, Theme::u32(c.text), label);
            const float nw = ImGui::GetFont()->CalcTextSizeA(15 * s, FLT_MAX, 0.0f, nums.c_str()).x;
            dl->AddText(ImGui::GetFont(), 15 * s, ImVec2(p0.x + fullW - nw, p0.y), Theme::u32(c.textMuted), nums.c_str());
            const float y = p0.y + 22 * s;
            const float total = static_cast<float>(std::max(1, t.games()));
            float x = p0.x;
            const float parts[3] = {t.w / total, t.d / total, t.l / total};
            const ImVec4 cols[3] = {c.qBest, c.textMuted, c.qBlunder};
            for (int i = 0; i < 3; ++i) {
                const float ww = fullW * parts[i];
                if (ww > 0) dl->AddRectFilled(ImVec2(x, y), ImVec2(x + ww, y + 6 * s), Theme::u32(cols[i]), 3 * s);
                x += ww;
            }
            ImGui::Dummy(ImVec2(fullW, 36 * s));
        };
        bar("Gesamt", all);
        if (asWhite.games()) bar("Mit Weiß", asWhite);
        if (asBlack.games()) bar("Mit Schwarz", asBlack);

        sectionTitle("Gegen die Bots");
        std::vector<std::pair<std::string, Tally>> opp(byOpponent.begin(), byOpponent.end());
        std::sort(opp.begin(), opp.end(), [](const auto& a, const auto& b) { return a.second.games() > b.second.games(); });
        for (const auto& [name, t] : opp) bar(name.c_str(), t);
    }

    // --- Genauigkeit ----------------------------------------------------------------------
    if (!accuracies.empty()) {
        sectionTitle("Genauigkeit");
        double sum = 0;
        for (double a : accuracies) sum += a;
        const double avg = sum / accuracies.size();
        const size_t recentN = std::min<size_t>(5, accuracies.size());
        double recent = 0;
        for (size_t i = accuracies.size() - recentN; i < accuracies.size(); ++i) recent += accuracies[i];
        recent /= static_cast<double>(recentN);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", avg);
        std::string a = buf;
        std::replace(a.begin(), a.end(), '.', ',');
        dl->AddText(hf, 30 * s, p0, Theme::u32(c.text), a.c_str());
        const float aw = hf->CalcTextSizeA(30 * s, FLT_MAX, 0.0f, a.c_str()).x;
        dl->AddText(ImGui::GetFont(), 15 * s, ImVec2(p0.x + aw + 8 * s, p0.y + 4 * s), Theme::u32(c.textMuted),
                    ("im Schnitt aus " + std::to_string(accuracies.size()) + " analysierten Partien").c_str());
        const double diff = recent - avg;
        std::snprintf(buf, sizeof(buf), "%+.1f", diff);
        std::string trend = std::string("letzte ") + std::to_string(recentN) + ": " + buf;
        std::replace(trend.begin(), trend.end(), '.', ',');
        dl->AddText(ImGui::GetFont(), 15 * s, ImVec2(p0.x + aw + 8 * s, p0.y + 22 * s),
                    Theme::u32(diff >= 0 ? c.qBest : c.qMistake), trend.c_str());
        ImGui::Dummy(ImVec2(0, 48 * s));

        // Fehlerverteilung
        const MoveQuality errs[] = {MoveQuality::Inaccuracy, MoveQuality::Mistake, MoveQuality::Miss, MoveQuality::Blunder};
        for (MoveQuality q : errs) {
            const int n = counts[static_cast<size_t>(q)];
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            theme_.qualityBadge(dl, q, ImVec2(rp.x + 9 * s, rp.y + 10 * s), 9 * s);
            dl->AddText(ImGui::GetFont(), 16 * s, ImVec2(rp.x + 26 * s, rp.y), Theme::u32(c.text), qualityName(q).c_str());
            const std::string v = std::to_string(n) + (accuracies.empty() ? "" : "  (" +
                                  std::to_string(static_cast<int>(std::lround(n / static_cast<double>(accuracies.size())))) +
                                  " pro Partie)");
            const float vw = ImGui::GetFont()->CalcTextSizeA(15 * s, FLT_MAX, 0.0f, v.c_str()).x;
            dl->AddText(ImGui::GetFont(), 15 * s, ImVec2(rp.x + ImGui::GetContentRegionAvail().x - vw, rp.y),
                        Theme::u32(c.textMuted), v.c_str());
            ImGui::Dummy(ImVec2(0, 26 * s));
        }
    }

    // --- Eroeffnungen ----------------------------------------------------------------------
    if (!byOpening.empty()) {
        sectionTitle("Deine Eröffnungen");
        std::vector<std::pair<std::string, Tally>> ops(byOpening.begin(), byOpening.end());
        std::sort(ops.begin(), ops.end(), [](const auto& a, const auto& b) { return a.second.games() > b.second.games(); });
        if (ops.size() > 8) ops.resize(8);
        for (const auto& [name, t] : ops) {
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const float fullW = ImGui::GetContentRegionAvail().x;
            const std::string right = std::to_string(t.games()) + "×, " + std::to_string(static_cast<int>(std::lround(t.score()))) + " %";
            const float rw = ImGui::GetFont()->CalcTextSizeA(15 * s, FLT_MAX, 0.0f, right.c_str()).x;
            dl->PushClipRect(p0, ImVec2(p0.x + fullW - rw - 10 * s, p0.y + 24 * s), true);
            dl->AddText(ImGui::GetFont(), 16 * s, p0, Theme::u32(c.text), name.c_str());
            dl->PopClipRect();
            dl->AddText(ImGui::GetFont(), 15 * s, ImVec2(p0.x + fullW - rw, p0.y), Theme::u32(c.textMuted), right.c_str());
            ImGui::Dummy(ImVec2(fullW, 26 * s));
        }
    }
    ImGui::EndChild();
}

}  // namespace chess::ui
