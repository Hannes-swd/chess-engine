// Startseite: schnell eine Partie beginnen, die naechste Aufgabe, eine Eroeffnung zum Ueben,
// die letzte Partie, die Aufgabe des Tages, ein passender Gegner und die letzten Partien.

#include "ui/gui_app.hpp"
#include "ui/i18n.hpp"

#include "app/environment.hpp"
#include "core/fen.hpp"
#include "core/notation.hpp"
#include "core/pgn.hpp"
#include "core/openings.hpp"
#include "ui/gui_internal.hpp"
#include "ui/icons.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>

namespace chess::ui {

using detail::kTimePresetCount;
using detail::kTimePresets;

namespace {

ImVec4 alpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

// "2026-11-27" fuer heute bzw. n Tage zurueck
std::string dayString(int daysBack) {
    const std::time_t t = std::time(nullptr) - static_cast<std::time_t>(daysBack) * 24 * 3600;
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

int dayNumber() { return static_cast<int>(std::time(nullptr) / (24 * 3600)); }

}  // namespace

void GuiApp::drawHome() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    if (ImGui::GetTime() - gamesScannedAt_ > 4.0) scanSavedGames();
    ImFont* hf = theme_.heading ? theme_.heading : ImGui::GetFont();
    ImFont* sf = theme_.strong ? theme_.strong : ImGui::GetFont();

    const float avail = ImGui::GetContentRegionAvail().x;
    const float W = std::min(avail - 40 * s, 1180 * s);
    const float x0 = ImGui::GetCursorScreenPos().x + (avail - W) * 0.5f;
    const float gap = 14 * s;
    const float pad = 16 * s;
    const float sideW = std::clamp(W * 0.25f, 240 * s, 300 * s);  // rechte Spalte (Aufgabe des Tages)
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto panel = [&](ImVec2 a, ImVec2 b) { dl->AddRectFilled(a, b, Theme::u32(c.surface), 12 * s); };
    auto title = [&](ImVec2 at, const char* text) { dl->AddText(sf, 17 * s, at, Theme::u32(c.text), text); };

    float y = ImGui::GetCursorScreenPos().y + 22 * s;

    // --- Kopf: du, deine Elo, Serie ---------------------------------------------------------
    {
        const float r = 20 * s;
        const ImVec2 av(x0 + r, y + r);
        dl->AddCircleFilled(av, r, Theme::u32(c.surfaceRaised));
        Theme::glyphCentered(dl, theme_.body, 24 * s, av, Theme::u32(c.textMuted), icon::person);
        dl->AddText(hf, 26 * s, ImVec2(x0 + 2 * r + 14 * s, y + 4 * s), Theme::u32(c.text), tr("Willkommen zurück"));
        const std::string elo = "Elo " + std::to_string(player_.rating);
        const float tw = hf->CalcTextSizeA(26 * s, FLT_MAX, 0.0f, tr("Willkommen zurück")).x;
        dl->AddText(hf, 20 * s, ImVec2(x0 + 2 * r + 26 * s + tw, y + 9 * s), Theme::u32(c.accent), elo.c_str());

        // Serie: an wie vielen Tagen in Folge (bis heute oder gestern) gespielt wurde
        std::set<std::string> days;
        for (const SavedGame& g : savedGames_) {
            const std::string stem = g.path.stem().string();
            if (stem.size() >= 10) days.insert(stem.substr(0, 10));
        }
        int streak = 0;
        int start = days.count(dayString(0)) ? 0 : 1;
        while (days.count(dayString(start + streak))) ++streak;
        const std::string st = std::to_string(streak) + (streak == 1 ? tr(" Tag in Folge") : tr(" Tage in Folge"));
        const ImVec2 ss = hf->CalcTextSizeA(22 * s, FLT_MAX, 0.0f, st.c_str());
        const float sx = x0 + W - ss.x;
        Theme::glyphCentered(dl, theme_.body, 26 * s, ImVec2(sx - 18 * s, y + r), Theme::u32(streak ? c.accent : c.textMuted),
                             icon::fire);
        dl->AddText(hf, 22 * s, ImVec2(sx, y + r - ss.y * 0.5f), Theme::u32(c.text), st.c_str());
        if (ImGui::IsMouseHoveringRect(ImVec2(sx - 32 * s, y), ImVec2(x0 + W, y + 2 * r)))
            ImGui::SetTooltip("%s", tr("Tage hintereinander mit mindestens einer gespeicherten Partie"));
        y += 2 * r + 22 * s;
    }

    // --- Reihe 1: Spielen + drei Karten -------------------------------------------------------
    const float playW = 250 * s;
    const float cardsW = W - playW - 3 * gap - 2 * pad;
    const float cardW = cardsW / 3;
    const float board = cardW - 2 * 12 * s;
    const float rowH = pad + board + 12 * s + 62 * s + 50 * s;
    panel(ImVec2(x0, y), ImVec2(x0 + W, y + rowH));
    {
        // Spielen
        const float px = x0 + pad;
        float py = y + pad;
        title(ImVec2(px, py), tr("Spielen"));
        py += 30 * s;
        struct Quick {
            const char* label;
            const char* kind;
            int preset;
        };
        auto presetOf = [&](const char* label) {
            for (int i = 0; i < kTimePresetCount; ++i)
                if (std::string(kTimePresets[i].label) == label) return i;
            return 0;
        };
        const Quick quick[3] = {{"1 min", "Bullet", presetOf("1+0")}, {"10 min", "Schnell", presetOf("10+0")},
                                {"3 + 2", "Blitz", presetOf("3+2")}};
        const float chipW = (playW - 2 * 6 * s) / 3;
        for (int i = 0; i < 3; ++i) {
            ImGui::SetCursorScreenPos(ImVec2(px + i * (chipW + 6 * s), py));
            const std::string id = "##hq" + std::to_string(i);
            if (timeChip(id.c_str(), quick[i].label, tr(quick[i].kind), tcPreset_ == quick[i].preset, ImVec2(chipW, 48 * s)))
                tcPreset_ = quick[i].preset;
        }
        py += 48 * s + 10 * s;
        ImGui::SetCursorScreenPos(ImVec2(px, py));
        ImGui::PushFont(theme_.strong, 18.0f);
        const bool hasBot = selectedProfile_ >= 0 && selectedProfile_ < static_cast<int>(profiles_.size());
        const std::string start = std::string(icon::start) + "  " + tr("Partie starten");
        if (primaryButton(start.c_str(), ImVec2(playW, 50 * s))) {
            startNewGame();
            mode_ = Mode::Play;
            playTab_ = 1;
        }
        ImGui::PopFont();
        if (ImGui::IsItemHovered() && hasBot)
            ImGui::SetTooltip("%s %s (%d)", tr("Gegen"), profiles_[static_cast<size_t>(selectedProfile_)].displayName(english()).c_str(),
                              estimatedBotElo(profiles_[static_cast<size_t>(selectedProfile_)], stockfish_ != nullptr));
        py += 50 * s + 10 * s;
        struct Entry {
            const Icon* glyph;
            const char* label;
            int what;
        };
        const Entry entries[4] = {{&icon::bot, "Gegen Bots", 0},
                                  {&icon::people, "Zu zweit am Brett", 1},
                                  {&icon::training, "Training", 2},
                                  {&icon::plugin, "Plugins", 3}};
        const float eh = (rowH - (py - y) - pad - 3 * 6 * s) / 4;
        for (const Entry& e : entries) {
            ImGui::SetCursorScreenPos(ImVec2(px, py));
            ImGui::PushID(e.label);
            const bool clicked = ImGui::InvisibleButton("##e", ImVec2(playW, eh));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            dl->AddRectFilled(ImVec2(px, py), ImVec2(px + playW, py + eh),
                              Theme::u32(hovered ? c.surfaceRaised : alpha(c.surfaceRaised, 0.55f)), 8 * s);
            const std::string label = tr(e.label);
            const ImVec2 ls = sf->CalcTextSizeA(17 * s, FLT_MAX, 0.0f, label.c_str());
            const float lx = px + (playW - ls.x) * 0.5f + 14 * s;
            Theme::glyphCentered(dl, theme_.body, 22 * s, ImVec2(lx - 18 * s, py + eh * 0.5f), Theme::u32(c.accent), *e.glyph);
            dl->AddText(sf, 17 * s, ImVec2(lx, py + (eh - ls.y) * 0.5f), Theme::u32(c.text), label.c_str());
            if (clicked) {
                switch (e.what) {
                    case 0: mode_ = Mode::Bots; break;
                    case 1:
                        selectedProfile_ = -1;
                        startNewGame();
                        mode_ = Mode::Play;
                        playTab_ = 1;
                        break;
                    case 2: mode_ = Mode::Training; break;
                    case 3: mode_ = Mode::Plugins; break;
                }
            }
            py += eh + 6 * s;
        }
    }

    // Eine Karte: Brett oben, darunter Symbol + Zeile, unten ein Knopf
    auto card = [&](int idx, const Position* pos, const Icon& glyph, const std::string& line1, const std::string& line2,
                    const char* button, bool enabled) {
        const float cx = x0 + pad + playW + gap + idx * (cardW + gap / 2);
        const float cy = y + pad;
        const ImVec2 b0(cx + 12 * s, cy);
        if (pos) {
            drawMiniBoard(dl, *pos, b0, board);
        } else {
            dl->AddRectFilled(b0, ImVec2(b0.x + board, b0.y + board), Theme::u32(c.surfaceRaised), 8 * s);
            Theme::glyphCentered(dl, theme_.body, board * 0.3f, ImVec2(b0.x + board * 0.5f, b0.y + board * 0.5f),
                                 Theme::u32(c.textMuted, 0.5f), glyph);
        }
        float ty = cy + board + 12 * s;
        Theme::glyphCentered(dl, theme_.body, 24 * s, ImVec2(b0.x + 12 * s, ty + 12 * s), Theme::u32(c.accent), glyph);
        dl->PushClipRect(ImVec2(b0.x, ty), ImVec2(b0.x + board, ty + 60 * s), true);
        dl->AddText(sf, 16 * s, ImVec2(b0.x + 32 * s, ty + 2 * s), Theme::u32(c.text), line1.c_str());
        dl->AddText(ImGui::GetFont(), 14.5f * s, ImVec2(b0.x + 32 * s, ty + 24 * s), Theme::u32(c.textMuted), line2.c_str());
        dl->PopClipRect();
        ty += 62 * s;
        ImGui::SetCursorScreenPos(ImVec2(b0.x, ty));
        ImGui::BeginDisabled(!enabled);
        ImGui::PushID(idx);
        const bool clicked = ImGui::Button(tr(button), ImVec2(board, 40 * s));
        ImGui::PopID();
        ImGui::EndDisabled();
        return clicked;
    };

    // Karte 1: naechste Aufgabe
    {
        const auto due = duePuzzles(training_, static_cast<long long>(std::time(nullptr)));
        std::optional<Position> pos;
        int next = -1;
        if (!training_.puzzles.empty()) {
            next = due.empty() ? 0 : static_cast<int>(due.front());
            try {
                pos = fromFen(PieceRegistry::standard(), training_.puzzles[static_cast<size_t>(next)].fen);
            } catch (const std::exception&) {
            }
        }
        const std::string l1 = std::to_string(due.size()) + (due.size() == 1 ? tr(" Aufgabe fällig") : tr(" Aufgaben fällig"));
        const std::string l2 = training_.puzzles.empty() ? tr("Entstehen aus deinen Fehlern")
                                                         : tr(training_.puzzles[static_cast<size_t>(next)].theme);
        if (card(0, pos ? &*pos : nullptr, icon::training, l1, l2, "Aufgaben lösen", !training_.puzzles.empty())) {
            mode_ = Mode::Training;
            trainTab_ = 0;
            startPuzzle();
        }
    }
    // Karte 2: Eroeffnung des Tages (wechselt taeglich)
    {
        std::vector<size_t> pool;
        for (size_t i = 0; i < openings().size(); ++i)
            if (openings()[i].moves.size() >= 6) pool.push_back(i);
        const size_t pick = pool[static_cast<size_t>(dayNumber()) % pool.size()];
        const Opening& o = openings()[pick];
        Position pos = Game().position();
        for (const std::string& san : o.moves)
            if (auto m = parseMove(pos, san)) pos.makeMove(*m);
        const auto runs = training_.openingRuns.find(o.name);
        const std::string l2 = runs == training_.openingRuns.end()
                                   ? std::string(tr("Eröffnung des Tages"))
                                   : std::to_string(runs->second) + tr("× geübt");
        if (card(1, &pos, icon::book, o.displayName(english()), l2, "Üben", true)) {
            mode_ = Mode::Training;
            trainTab_ = 1;
            startOpeningDrill(pick);
        }
    }
    // Karte 3: letzte Partie
    {
        const SavedGame* last = savedGames_.empty() ? nullptr : &savedGames_.front();
        if (last && homeLastPath_ != last->path) {
            homeLastPath_ = last->path;
            homeLastPos_.reset();
            std::ifstream in(last->path, std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            if (const auto rec = parsePgn(ss.str()))
                try {
                    homeLastPos_ = gameFromPgn(*rec).position();
                } catch (const std::exception&) {
                }
        }
        std::string l1 = tr("Noch keine Partie"), l2;
        if (last) {
            const bool youWhite = last->white == "Du";
            l1 = std::string(tr("gegen ")) + displayPlayer(youWhite ? last->black : last->white);
            const bool win = (youWhite && last->result == "1-0") || (last->black == "Du" && last->result == "0-1");
            const bool loss = (youWhite && last->result == "0-1") || (last->black == "Du" && last->result == "1-0");
            l2 = std::string(win ? tr("Gewonnen") : loss ? tr("Verloren") : last->result == "1/2-1/2" ? tr("Remis") : "") +
                 (last->accuracyYou ? "   " + std::to_string(static_cast<int>(std::lround(*last->accuracyYou))) + " %" : "");
        }
        if (card(2, homeLastPos_ ? &*homeLastPos_ : nullptr, icon::history, l1, l2, "Partie ansehen", last != nullptr))
            openSavedGame(last->path);
    }
    y += rowH + gap;

    // --- Reihe 2: links empfohlener Gegner + letzte Partien, rechts Aufgabe des Tages --------------
    const float leftW = W - sideW - gap;
    const float rowTop = y;

    // Empfohlener Gegner: der Bot mit der Elo am naechsten an deiner
    {
        int best = -1, bestDiff = 1 << 30;
        for (size_t i = 0; i < profiles_.size(); ++i) {
            const int d = std::abs(estimatedBotElo(profiles_[i], stockfish_ != nullptr) - player_.rating);
            if (d < bestDiff) best = static_cast<int>(i), bestDiff = d;
        }
        const float h = 96 * s;
        panel(ImVec2(x0, y), ImVec2(x0 + leftW, y + h));
        title(ImVec2(x0 + pad, y + pad), tr("Empfohlener Gegner"));
        if (best >= 0) {
            const BotProfile& p = profiles_[static_cast<size_t>(best)];
            const float r = 22 * s;
            drawBotAvatar(dl, ImVec2(x0 + pad + r, y + pad + 28 * s + r * 0.6f), r, &p);
            const float tx = x0 + pad + 2 * r + 14 * s;
            const std::string name = p.displayName(english()) + "   " + std::to_string(estimatedBotElo(p, stockfish_ != nullptr));
            dl->AddText(sf, 16 * s, ImVec2(tx, y + pad + 28 * s), Theme::u32(c.text), name.c_str());
            const int win = eloDelta(player_.rating, estimatedBotElo(p, stockfish_ != nullptr), 1.0, player_.games);
            const std::string sub = std::string(tr("Ähnlich stark wie du – ein Sieg bringt etwa +")) + std::to_string(win);
            dl->AddText(ImGui::GetFont(), 14.5f * s, ImVec2(tx, y + pad + 50 * s), Theme::u32(c.textMuted), sub.c_str());
            ImGui::SetCursorScreenPos(ImVec2(x0 + leftW - pad - 150 * s, y + pad + 26 * s));
            ImGui::PushFont(theme_.strong, 0.0f);
            if (primaryButton(tr("Herausfordern"), ImVec2(150 * s, 40 * s))) {
                selectedProfile_ = best;
                startNewGame();
                mode_ = Mode::Play;
                playTab_ = 1;
            }
            ImGui::PopFont();
        }
        y += h + gap;
    }

    // Letzte Partien als Tabelle
    {
        const size_t rows = std::min<size_t>(savedGames_.size(), 8);
        const float rowH2 = 40 * s;
        const float h = pad + 30 * s + 26 * s + std::max<size_t>(rows, 1) * rowH2 + pad;
        panel(ImVec2(x0, y), ImVec2(x0 + leftW, y + h));
        title(ImVec2(x0 + pad, y + pad), tr("Letzte Partien"));
        float ty = y + pad + 32 * s;
        const float colPlayers = x0 + pad, colResult = x0 + leftW * 0.50f, colAcc = x0 + leftW * 0.62f,
                    colMoves = x0 + leftW * 0.74f, colDate = x0 + leftW * 0.83f;
        ImFont* f = ImGui::GetFont();
        auto head = [&](float x, const char* t) { dl->AddText(f, 13.5f * s, ImVec2(x, ty), Theme::u32(c.textMuted), t); };
        head(colPlayers, tr("Spieler"));
        head(colResult, tr("Ergebnis"));
        head(colAcc, tr("Genauigkeit"));
        head(colMoves, tr("Züge"));
        head(colDate, tr("Datum"));
        ty += 24 * s;
        dl->AddLine(ImVec2(x0 + pad, ty), ImVec2(x0 + leftW - pad, ty), Theme::u32(c.border));
        if (rows == 0)
            dl->AddText(f, 15 * s, ImVec2(colPlayers, ty + 12 * s), Theme::u32(c.textMuted),
                        tr("Noch keine gespeicherten Partien."));
        for (size_t i = 0; i < rows; ++i) {
            const SavedGame& g = savedGames_[i];
            const float ry = ty + i * rowH2;
            ImGui::SetCursorScreenPos(ImVec2(x0 + pad, ry));
            ImGui::PushID(static_cast<int>(500 + i));
            const bool clicked = ImGui::InvisibleButton("##hist", ImVec2(leftW - 2 * pad, rowH2));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            if (hovered) {
                dl->AddRectFilled(ImVec2(x0 + pad - 6 * s, ry), ImVec2(x0 + leftW - pad + 6 * s, ry + rowH2),
                                  Theme::u32(c.surfaceRaised), 6 * s);
                ImGui::SetTooltip("%s", tr("Öffnen und analysieren"));
            }
            const float my = ry + rowH2 * 0.5f;
            // Spieler: Farbscheibe + Name, du hervorgehoben
            auto who = [&](const std::string& name, bool white, float yy) {
                dl->AddCircleFilled(ImVec2(colPlayers + 5 * s, yy), 4.5f * s, Theme::u32(white ? c.whiteFill : c.blackFill));
                dl->AddCircle(ImVec2(colPlayers + 5 * s, yy), 4.5f * s, Theme::u32(c.textMuted, 0.6f));
                dl->AddText(name == "Du" ? sf : f, 14.5f * s, ImVec2(colPlayers + 16 * s, yy - 9 * s),
                            Theme::u32(name == "Du" ? c.text : c.textMuted), displayPlayer(name).c_str());
            };
            who(g.white, true, my - 9 * s);
            who(g.black, false, my + 9 * s);
            const bool youW = g.white == "Du", youB = g.black == "Du";
            ImVec4 tone = c.textMuted;
            if ((youW && g.result == "1-0") || (youB && g.result == "0-1")) tone = c.qBest;
            if ((youW && g.result == "0-1") || (youB && g.result == "1-0")) tone = c.qBlunder;
            const std::string res = g.result == "1/2-1/2" ? "½–½" : g.result.empty() ? "–" : g.result;
            dl->AddText(hf, 17 * s, ImVec2(colResult, my - 10 * s), Theme::u32(tone), res.c_str());
            const std::string acc = g.accuracyYou ? std::to_string(static_cast<int>(std::lround(*g.accuracyYou))) + " %" : "–";
            dl->AddText(f, 15 * s, ImVec2(colAcc, my - 9 * s), Theme::u32(c.text), acc.c_str());
            dl->AddText(f, 15 * s, ImVec2(colMoves, my - 9 * s), Theme::u32(c.text), std::to_string(g.moves).c_str());
            dl->AddText(f, 15 * s, ImVec2(colDate, my - 9 * s), Theme::u32(c.textMuted), g.date.substr(0, 10).c_str());
            if (clicked) openSavedGame(g.path);
        }
        y += h + gap;
    }

    // Rechts: Aufgabe des Tages (aus deinen eigenen Aufgaben, wechselt taeglich)
    {
        const float sx = x0 + leftW + gap;
        const float sb = sideW - 2 * pad;
        const float h = pad + 30 * s + sb + 70 * s + 50 * s;
        panel(ImVec2(sx, rowTop), ImVec2(sx + sideW, rowTop + h));
        title(ImVec2(sx + pad, rowTop + pad), tr("Aufgabe des Tages"));
        const float by = rowTop + pad + 30 * s;
        if (training_.puzzles.empty()) {
            dl->AddRectFilled(ImVec2(sx + pad, by), ImVec2(sx + pad + sb, by + sb), Theme::u32(c.surfaceRaised), 8 * s);
            dl->AddText(ImGui::GetFont(), 15 * s, ImVec2(sx + pad, by + sb + 12 * s), Theme::u32(c.textMuted),
                        tr("Spiel und analysiere eine Partie –"), nullptr);
            dl->AddText(ImGui::GetFont(), 15 * s, ImVec2(sx + pad, by + sb + 32 * s), Theme::u32(c.textMuted),
                        tr("deine Fehler werden zu Aufgaben."));
        } else {
            const size_t idx = static_cast<size_t>(dayNumber()) % training_.puzzles.size();
            const Puzzle& pz = training_.puzzles[idx];
            try {
                Position pos = fromFen(PieceRegistry::standard(), pz.fen);
                drawMiniBoard(dl, pos, ImVec2(sx + pad, by), sb);
                const std::string side = pos.sideToMove() == Color::White ? tr("Weiß am Zug") : tr("Schwarz am Zug");
                dl->AddText(sf, 16 * s, ImVec2(sx + pad, by + sb + 12 * s), Theme::u32(c.text), side.c_str());
                dl->PushClipRect(ImVec2(sx + pad, by + sb), ImVec2(sx + sideW - pad, by + sb + 70 * s), true);
                dl->AddText(ImGui::GetFont(), 14.5f * s, ImVec2(sx + pad, by + sb + 34 * s), Theme::u32(c.textMuted),
                            displaySource(pz.source).c_str());
                dl->PopClipRect();
            } catch (const std::exception&) {
            }
            ImGui::SetCursorScreenPos(ImVec2(sx + pad, by + sb + 70 * s));
            ImGui::PushFont(theme_.strong, 0.0f);
            if (primaryButton(tr("Aufgabe des Tages lösen"), ImVec2(sb, 42 * s))) {
                mode_ = Mode::Training;
                trainTab_ = 0;
                startPuzzle(static_cast<int>(idx));
            }
            ImGui::PopFont();
        }
        y = std::max(y, rowTop + h + gap);
    }

    // Platz fuer das Scrollen reservieren
    ImGui::SetCursorScreenPos(ImVec2(x0, y));
    ImGui::Dummy(ImVec2(W, 10 * s));
}

}  // namespace chess::ui
