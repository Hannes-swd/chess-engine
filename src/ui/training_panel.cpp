// Bereich "Training":
//  - Aufgaben aus den eigenen Fehlern (Karteikasten-Wiederholung)
//  - Eroeffnungs-Training: die Zuege einer Eroeffnung auswendig spielen, der Gegner antwortet nach Theorie

#include "ui/gui_app.hpp"

#include "app/environment.hpp"
#include "core/fen.hpp"
#include "core/notation.hpp"
#include "core/openings.hpp"
#include "engine/engine.hpp"
#include "ui/icons.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>

namespace chess::ui {

namespace {

ImVec4 alpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

long long nowSeconds() { return static_cast<long long>(std::time(nullptr)); }

std::string lower(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

}  // namespace

// ===========================================================================
// Daten
// ===========================================================================

void GuiApp::loadTraining() {
    std::ifstream in(env::trainingFile(), std::ios::binary);
    if (!in) return;
    std::stringstream ss;
    ss << in.rdbuf();
    trainingFromJson(ss.str(), training_);
}

void GuiApp::saveTraining() {
    if (sandbox_) return;
    std::ofstream out(env::trainingFile(), std::ios::binary);
    out << trainingToJson(training_);
}

void GuiApp::collectPuzzles() {
    if (!analysis_ || collectedGen_ == generation_ || !usesStandardRules(game_)) return;
    collectedGen_ = generation_;
    // Nur eigene Fehler: gegen Bots die eigene Farbe, in geoeffneten Partien die Seite "Du",
    // zu zweit am Brett beide Seiten
    std::optional<Color> side;
    if (bot_) {
        side = opposite(botColor_);
    } else if (!loadedTags_.empty()) {
        if (playerName(Color::White) == "Du")
            side = Color::White;
        else if (playerName(Color::Black) == "Du")
            side = Color::Black;
        else
            return;
    }
    std::string date = env::timestamp().substr(0, 10);
    if (date.size() == 10) date = date.substr(8, 2) + "." + date.substr(5, 2) + "." + date.substr(0, 4);
    const std::string source = playerName(Color::White) + " – " + playerName(Color::Black) + ", " + date;
    const int added = mergePuzzles(training_, puzzlesFromAnalysis(game_, *analysis_, side, source, nowSeconds()));
    if (added > 0) {
        saveTraining();
        setStatus(std::to_string(added) + (added == 1 ? " neue Aufgabe" : " neue Aufgaben") +
                  " aus deinen Fehlern (Training)");
    }
}

// ===========================================================================
// Aufgaben und Eroeffnungen starten
// ===========================================================================

void GuiApp::startPuzzle() {
    drill_.reset();
    if (training_.puzzles.empty()) return;
    auto due = duePuzzles(training_, nowSeconds());
    size_t pick = 0;
    if (!due.empty()) {
        pick = due.front();
    } else {  // nichts faellig: die naechste vorziehen
        for (size_t i = 1; i < training_.puzzles.size(); ++i)
            if (training_.puzzles[i].due < training_.puzzles[pick].due) pick = i;
    }
    const Puzzle& p = training_.puzzles[pick];
    try {
        Drill d;
        d.start = fromFen(PieceRegistry::standard(), p.fen);
        d.current = d.start;
        Position tmp = d.start;
        const auto best = parseUciMove(tmp, p.bestUci);
        if (!best) throw std::runtime_error("Lösungszug ungültig");
        d.line = {*best};
        d.user = d.start.sideToMove();
        d.title = "Finde den besseren Zug";
        d.subtitle = "In der Partie kam " + figurine(p.playedSan) + " – " + p.theme + ".";
        d.puzzle = static_cast<int>(pick);
        drill_ = std::move(d);
        board_.cancel();
    } catch (const std::exception& e) {
        setStatus(std::string("Aufgabe fehlerhaft: ") + e.what());
        training_.puzzles.erase(training_.puzzles.begin() + static_cast<long>(pick));
        saveTraining();
    }
}

void GuiApp::startOpeningDrill(size_t index) {
    drill_.reset();
    if (index >= openings().size()) return;
    const Opening& o = openings()[index];
    Drill d;
    d.start = Game().position();
    d.current = d.start;
    Position walk = d.start;
    for (const std::string& san : o.moves) {
        const auto m = parseMove(walk, san);
        if (!m) break;
        d.line.push_back(*m);
        walk.makeMove(*m);
    }
    d.user = o.forBlack ? Color::Black : Color::White;
    d.title = o.name;
    d.subtitle = std::string("Du spielst ") + (o.forBlack ? "Schwarz" : "Weiß") + ". Spiele die Züge der Theorie.";
    d.opening = static_cast<int>(index);
    d.replyAt = ImGui::GetTime() + 0.5;
    drill_ = std::move(d);
    board_.cancel();
}

// ===========================================================================
// Ablauf
// ===========================================================================

void GuiApp::drillMove(const Move& m) {
    if (!drill_ || drill_->done) return;
    Drill& d = *drill_;
    const Move& want = d.line[static_cast<size_t>(d.index)];
    const double now = ImGui::GetTime();
    if (m == want) {
        const Position before = d.current;
        d.current.makeMove(want);
        if (settings_.animations) board_.animate(want);
        d.flash = want;
        d.flashGood = true;
        d.flashUntil = now + 0.6;
        ++d.index;
        d.revealed = false;
        if (d.index >= static_cast<int>(d.line.size())) {
            d.done = true;
            const bool clean = d.mistakes == 0;
            playSound(clean ? Sound::Kind::Correct : Sound::Kind::Move);
            if (d.puzzle >= 0 && d.puzzle < static_cast<int>(training_.puzzles.size())) {
                schedule(training_.puzzles[static_cast<size_t>(d.puzzle)], clean, nowSeconds());
                d.feedback = clean ? "Richtig! Diese Aufgabe kommt in ein paar Tagen wieder."
                                   : "Geschafft – sie kommt bald noch einmal, bis sie sitzt.";
            } else if (d.opening >= 0) {
                const std::string& name = openings()[static_cast<size_t>(d.opening)].name;
                ++training_.openingRuns[name];
                if (clean) ++training_.openingClean[name];
                d.feedback = clean ? "Fehlerfrei – die Linie sitzt." : "Linie geschafft, mit " +
                                                                         std::to_string(d.mistakes) + " Fehlversuch" +
                                                                         (d.mistakes == 1 ? "." : "en.");
            }
            saveTraining();
        } else {
            playSounds(before, want);
            d.feedback = d.opening >= 0 ? "Richtig." : d.feedback;
            d.replyAt = now + 0.45;  // Gegner antwortet gleich
        }
    } else {
        ++d.mistakes;
        d.flash = m;
        d.flashGood = false;
        d.flashUntil = now + 0.8;
        playSound(Sound::Kind::Wrong);
        if (d.mistakes >= 2) d.revealed = true;
        d.feedback = d.revealed ? "Nicht ganz. Der Pfeil zeigt den gesuchten Zug." : "Nicht ganz – versuch es noch einmal.";
    }
}

void GuiApp::tickDrill() {
    if (!drill_ || drill_->done) return;
    Drill& d = *drill_;
    if (d.index >= static_cast<int>(d.line.size())) return;
    if (d.current.sideToMove() == d.user || ImGui::GetTime() < d.replyAt) return;
    const Move m = d.line[static_cast<size_t>(d.index)];
    const Position before = d.current;
    d.current.makeMove(m);
    if (settings_.animations && mode_ == Mode::Training) board_.animate(m);
    if (mode_ == Mode::Training) {
        // Klang ohne Partie: Schlag oder normaler Zug
        if (d.current.inCheck(d.current.sideToMove()))
            playSound(Sound::Kind::Check);
        else
            playSound((m.flags & FlagCapture) ? Sound::Kind::Capture : Sound::Kind::Move);
    }
    (void)before;
    ++d.index;
    if (d.index >= static_cast<int>(d.line.size())) {
        d.done = true;
        d.feedback = "Linie geschafft.";
    }
}

// ===========================================================================
// Brett im Training
// ===========================================================================

void GuiApp::drawDrillBoard() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    Drill& d = *drill_;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float barH = 34.0f * s, gap = 8.0f * s;
    const double now = ImGui::GetTime();

    const bool interactive = !d.done && d.current.sideToMove() == d.user && now >= d.replyAt;
    const std::vector<Move> legal = interactive ? d.current.legalMoves() : std::vector<Move>{};
    BoardOverlay ov;
    ov.showCoordinates = settings_.coordinates;
    ov.showHints = settings_.legalMoves;
    ov.showDanger = false;  // im Training nichts verraten
    ov.showCheck = settings_.checkGlow;
    ov.showTargetFrame = settings_.targetFrame;
    ov.allowAnnotations = settings_.annotations;
    if (d.index > 0) ov.lastMove = d.line[static_cast<size_t>(d.index - 1)];
    if (d.flash && now < d.flashUntil) {
        ov.flashMove = d.flash;
        ov.flashGood = d.flashGood;
    }
    if (d.revealed && !d.done && d.index < static_cast<int>(d.line.size()))
        ov.arrows.push_back({d.line[static_cast<size_t>(d.index)], Theme::u32(c.arrow)});

    const ImVec2 boardOrigin(origin.x, origin.y + barH + gap);
    const ImVec2 boardAvail(avail.x, avail.y - 2 * (barH + gap));
    if (auto mv = board_.draw(d.current, legal, interactive, d.user == Color::Black, ov, theme_, boardOrigin, boardAvail))
        drillMove(*mv);

    // Oben: Aufgabe/Eroeffnung, unten: wer am Zug ist und die Rueckmeldung
    const ImVec2 bmin = board_.boardMin(), bmax = board_.boardMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* hf = theme_.heading ? theme_.heading : ImGui::GetFont();
    const ImVec2 tsz = hf->CalcTextSizeA(21 * s, FLT_MAX, 0.0f, d.title.c_str());
    Theme::glyphCentered(dl, theme_.body, 22 * s, ImVec2(bmin.x + 11 * s, bmin.y - gap - barH * 0.5f),
                         Theme::u32(c.accent), d.opening >= 0 ? icon::book : icon::training);
    dl->AddText(hf, 21 * s, ImVec2(bmin.x + 30 * s, bmin.y - gap - barH * 0.5f - tsz.y * 0.5f), Theme::u32(c.text),
                d.title.c_str());
    if (d.opening >= 0) {
        const std::string prog = std::to_string(d.index) + " / " + std::to_string(d.line.size());
        const ImVec2 psz = hf->CalcTextSizeA(18 * s, FLT_MAX, 0.0f, prog.c_str());
        dl->AddText(hf, 18 * s, ImVec2(bmax.x - psz.x, bmin.y - gap - barH * 0.5f - psz.y * 0.5f),
                    Theme::u32(c.textMuted), prog.c_str());
    }
    const float y = bmax.y + gap + barH * 0.5f;
    const ImVec4 fbCol = d.done ? c.qBest : (d.flash && !d.flashGood && now < d.flashUntil + 2.0) ? c.qMistake : c.textMuted;
    std::string line = d.feedback;
    if (line.empty()) line = interactive ? std::string(d.user == Color::White ? "Weiß" : "Schwarz") + " am Zug" : "";
    ImFont* sf = theme_.strong ? theme_.strong : ImGui::GetFont();
    const ImVec2 lsz = sf->CalcTextSizeA(17 * s, FLT_MAX, 0.0f, line.c_str());
    dl->AddText(sf, 17 * s, ImVec2(bmin.x, y - lsz.y * 0.5f), Theme::u32(fbCol), line.c_str());
}

// ===========================================================================
// Panel
// ===========================================================================

void GuiApp::panelTraining() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const float w = ImGui::GetContentRegionAvail().x / 2;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
    if (iconTab(icon::training, "Aufgaben", trainTab_ == 0, w) && trainTab_ != 0) {
        trainTab_ = 0;
        drill_.reset();
    }
    ImGui::SameLine();
    if (iconTab(icon::book, "Eröffnungen", trainTab_ == 1, w) && trainTab_ != 1) {
        trainTab_ = 1;
        drill_.reset();
    }
    ImGui::PopStyleVar();
    const ImVec2 lp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(lp.x, lp.y - 4 * s), ImVec2(lp.x + 2 * w, lp.y - 4 * s),
                                        Theme::u32(c.border));
    ImGui::Dummy(ImVec2(0, 4 * s));
    ImGui::BeginChild("##train", ImVec2(0, 0), ImGuiChildFlags_None);
    ImFont* hf = theme_.heading ? theme_.heading : ImGui::GetFont();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (trainTab_ == 0) {
        // --- Aufgaben ---------------------------------------------------------------
        const size_t total = training_.puzzles.size();
        const size_t due = duePuzzles(training_, nowSeconds()).size();
        {
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const std::string big = std::to_string(due);
            dl->AddText(hf, 40 * s, p0, Theme::u32(due ? c.accent : c.text), big.c_str());
            const float bw = hf->CalcTextSizeA(40 * s, FLT_MAX, 0.0f, big.c_str()).x;
            dl->AddText(theme_.strong, 17 * s, ImVec2(p0.x + bw + 10 * s, p0.y + 6 * s), Theme::u32(c.text),
                        due == 1 ? "Aufgabe fällig" : "Aufgaben fällig");
            const std::string sub = "von " + std::to_string(total) + " aus deinen Partien";
            dl->AddText(ImGui::GetFont(), 15 * s, ImVec2(p0.x + bw + 10 * s, p0.y + 27 * s), Theme::u32(c.textMuted),
                        sub.c_str());
            ImGui::Dummy(ImVec2(0, 54 * s));
        }

        if (total == 0) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(c.textMuted, "%s",
                               "Noch keine Aufgaben. Sobald eine deiner Partien analysiert ist, wird jeder Fehler "
                               "und Patzer zu einer Aufgabe: Finde den Zug, den du hättest spielen sollen. "
                               "Was du löst, kommt erst nach Tagen wieder, was nicht klappt, schon bald.");
            ImGui::PopTextWrapPos();
        } else if (drill_ && drill_->puzzle >= 0) {
            const Drill& d = *drill_;
            const Puzzle& p = training_.puzzles[static_cast<size_t>(d.puzzle)];
            sectionTitle("Diese Aufgabe");
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(d.subtitle.c_str());
            ImGui::TextColored(c.textMuted, "%s", p.source.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, 6 * s));
            if (d.done) {
                ImGui::TextColored(c.qBest, "Gesucht war %s.", figurine(p.bestSan).c_str());
                ImGui::PushFont(theme_.strong, 0.0f);
                if (primaryButton((std::string(icon::next) + "  Nächste Aufgabe").c_str(), ImVec2(-FLT_MIN, 44 * s)))
                    startPuzzle();
                ImGui::PopFont();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                if (ImGui::Button((std::string(icon::visibility) + "  Lösung zeigen").c_str())) {
                    drill_->revealed = true;
                    drill_->mistakes = std::max(drill_->mistakes, 1);
                }
                ImGui::SameLine();
                if (ImGui::Button((std::string(icon::next) + "  Überspringen").c_str())) startPuzzle();
                ImGui::PopStyleColor();
            }
        } else {
            ImGui::PushFont(theme_.strong, 0.0f);
            const char* label = due ? "Aufgaben lösen" : "Nächste Aufgabe vorziehen";
            if (primaryButton((std::string(icon::start) + "  " + label).c_str(), ImVec2(-FLT_MIN, 46 * s)))
                startPuzzle();
            ImGui::PopFont();
        }

        // Fehlerarten: was passiert dir am haeufigsten?
        if (total > 0) {
            std::map<std::string, int> themes;
            for (const Puzzle& p : training_.puzzles) ++themes[p.theme];
            std::vector<std::pair<std::string, int>> sorted(themes.begin(), themes.end());
            std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            sectionTitle("Deine häufigsten Fehler");
            const float fullW = ImGui::GetContentRegionAvail().x;
            for (const auto& [name, n] : sorted) {
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                const float frac = static_cast<float>(n) / static_cast<float>(sorted.front().second);
                dl->AddRectFilled(ImVec2(p0.x, p0.y + 22 * s), ImVec2(p0.x + fullW, p0.y + 26 * s),
                                  Theme::u32(alpha(c.surfaceRaised, 1.0f)), 2 * s);
                dl->AddRectFilled(ImVec2(p0.x, p0.y + 22 * s), ImVec2(p0.x + fullW * frac, p0.y + 26 * s),
                                  Theme::u32(c.qMistake), 2 * s);
                dl->AddText(ImGui::GetFont(), 16 * s, p0, Theme::u32(c.text), name.c_str());
                const std::string cnt = std::to_string(n) + "×";
                const float cw = ImGui::GetFont()->CalcTextSizeA(16 * s, FLT_MAX, 0.0f, cnt.c_str()).x;
                dl->AddText(ImGui::GetFont(), 16 * s, ImVec2(p0.x + fullW - cw, p0.y), Theme::u32(c.textMuted),
                            cnt.c_str());
                ImGui::Dummy(ImVec2(fullW, 32 * s));
            }

            // Karteikasten: wie gut sitzen die Aufgaben?
            sectionTitle("Karteikasten");
            int boxes[6] = {};
            for (const Puzzle& p : training_.puzzles) ++boxes[std::clamp(p.box, 0, 5)];
            static const char* names[6] = {"neu", "1 Tag", "3 Tage", "1 Woche", "3 Wochen", "sitzt"};
            const float bw = (ImGui::GetContentRegionAvail().x - 5 * 4 * s) / 6;
            const ImVec2 b0 = ImGui::GetCursorScreenPos();
            for (int i = 0; i < 6; ++i) {
                const ImVec2 a(b0.x + i * (bw + 4 * s), b0.y), b(a.x + bw, a.y + 50 * s);
                dl->AddRectFilled(a, b, Theme::u32(alpha(i == 5 ? c.qBest : c.surfaceRaised, i == 5 ? 0.2f : 1.0f)),
                                  6 * s);
                const std::string v = std::to_string(boxes[i]);
                const ImVec2 vs = hf->CalcTextSizeA(20 * s, FLT_MAX, 0.0f, v.c_str());
                dl->AddText(hf, 20 * s, ImVec2((a.x + b.x - vs.x) * 0.5f, a.y + 5 * s), Theme::u32(c.text), v.c_str());
                const ImVec2 ns = ImGui::GetFont()->CalcTextSizeA(12.5f * s, FLT_MAX, 0.0f, names[i]);
                dl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2((a.x + b.x - ns.x) * 0.5f, a.y + 30 * s),
                            Theme::u32(c.textMuted), names[i]);
            }
            ImGui::Dummy(ImVec2(0, 56 * s));
        }
    } else {
        // --- Eroeffnungen ---------------------------------------------------------------
        if (drill_ && drill_->opening >= 0) {
            const Drill& d = *drill_;
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(d.subtitle.c_str());
            ImGui::PopTextWrapPos();
            // Linie als Text, bis zur aktuellen Stelle sichtbar
            std::string seen;
            Position walk = d.start;
            for (int i = 0; i < d.index; ++i) {
                const Move& m = d.line[static_cast<size_t>(i)];
                if (walk.sideToMove() == Color::White) seen += std::to_string(walk.fullmoveNumber()) + ". ";
                seen += figurine(toSan(walk, m)) + " ";
                walk.makeMove(m);
            }
            if (!seen.empty()) {
                ImGui::PushTextWrapPos(0);
                ImGui::TextColored(c.textMuted, "%s", seen.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::Dummy(ImVec2(0, 6 * s));
            ImGui::PushFont(theme_.strong, 0.0f);
            if (d.done) {
                if (primaryButton((std::string(icon::replay) + "  Nochmal").c_str(), ImVec2(-FLT_MIN, 44 * s)))
                    startOpeningDrill(static_cast<size_t>(d.opening));
            }
            ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (!d.done && ImGui::Button((std::string(icon::visibility) + "  Zug zeigen").c_str())) {
                drill_->revealed = true;
                ++drill_->mistakes;
            }
            if (!d.done) ImGui::SameLine();
            if (ImGui::Button((std::string(icon::prev) + "  Andere Eröffnung").c_str())) drill_.reset();
            ImGui::PopStyleColor();
        } else {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(c.textMuted, "%s",
                               "Wähle eine Eröffnung. Du spielst ihre Züge, der Gegner antwortet nach Theorie. "
                               "Ein falscher Zug wird angezeigt, du versuchst es noch einmal.");
            ImGui::PopTextWrapPos();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##opfilter", "Suchen, z. B. Sizilianisch", openingFilter_, sizeof(openingFilter_));
            const std::string filter = lower(openingFilter_);
            ImGui::BeginChild("##oplist", ImVec2(0, 0), ImGuiChildFlags_None);
            const auto& all = openings();
            for (size_t i = 0; i < all.size(); ++i) {
                const Opening& o = all[i];
                if (o.moves.size() < 3) continue;  // erste Zuege allein sind keine Uebung
                if (!filter.empty() && lower(o.name).find(filter) == std::string::npos) continue;
                const float rw = ImGui::GetContentRegionAvail().x, rh = 48 * s;
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImGui::PushID(static_cast<int>(i));
                const bool clicked = ImGui::InvisibleButton("##op", ImVec2(rw, rh));
                const bool hovered = ImGui::IsItemHovered();
                ImGui::PopID();
                ImDrawList* ldl = ImGui::GetWindowDrawList();
                if (hovered) ldl->AddRectFilled(p0, ImVec2(p0.x + rw, p0.y + rh), Theme::u32(alpha(c.surfaceRaised, 0.7f)), 8 * s);
                // Farbe als kleine Scheibe
                const ImVec2 disc(p0.x + 14 * s, p0.y + rh * 0.5f);
                ldl->AddCircleFilled(disc, 7 * s, Theme::u32(o.forBlack ? c.blackFill : c.whiteFill));
                ldl->AddCircle(disc, 7 * s, Theme::u32(c.textMuted, 0.7f), 0, 1.2f * s);
                ldl->PushClipRect(p0, ImVec2(p0.x + rw - 8 * s, p0.y + rh), true);
                ldl->AddText(theme_.strong, 16 * s, ImVec2(p0.x + 30 * s, p0.y + 5 * s), Theme::u32(c.text), o.name.c_str());
                std::string meta = std::to_string((o.moves.size() + 1) / 2) + " Züge";
                if (auto it = training_.openingRuns.find(o.name); it != training_.openingRuns.end())
                    meta += ", " + std::to_string(it->second) + "× geübt, " +
                            std::to_string(training_.openingClean[o.name]) + "× fehlerfrei";
                ldl->AddText(ImGui::GetFont(), 14 * s, ImVec2(p0.x + 30 * s, p0.y + 26 * s), Theme::u32(c.textMuted),
                             meta.c_str());
                ldl->PopClipRect();
                if (clicked) startOpeningDrill(i);
            }
            ImGui::EndChild();
        }
    }
    ImGui::EndChild();
}

}  // namespace chess::ui
