// Bereich "Partien": gespeicherte Partien wieder oeffnen (zum Nachspielen und Analysieren),
// PGN aus der Zwischenablage einlesen und die aktuelle Partie exportieren.

#include "ui/gui_app.hpp"
#include "ui/i18n.hpp"

#include "app/environment.hpp"
#include "core/fen.hpp"
#include "core/openings.hpp"
#include "ui/icons.hpp"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <sstream>

namespace chess::ui {

namespace fs = std::filesystem;

namespace {

ImVec4 alpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// "2026-11-26_185514.pgn" -> "26.11. 18:55"; sonst das Datum aus dem Tag ("2026.11.26" -> "26.11.2026")
std::string displayDate(const fs::path& path, const std::string& dateTag) {
    const std::string stem = path.stem().string();
    if (stem.size() >= 15 && stem[4] == '-' && stem[7] == '-' && stem[10] == '_')
        return stem.substr(8, 2) + "." + stem.substr(5, 2) + "." + stem.substr(0, 4) + ", " + stem.substr(11, 2) + ":" +
               stem.substr(13, 2);
    if (dateTag.size() == 10 && dateTag.find('?') == std::string::npos)
        return dateTag.substr(8, 2) + "." + dateTag.substr(5, 2) + "." + dateTag.substr(0, 4);
    return path.stem().string();
}

}  // namespace

void GuiApp::scanSavedGames() {
    gamesScannedAt_ = ImGui::GetTime();
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(env::gamesDir(), ec))
        if (e.is_regular_file() && e.path().extension() == ".pgn") files.push_back(e.path());
    std::sort(files.begin(), files.end(), std::greater<>());  // Dateinamen sind Zeitstempel: neueste zuerst
    if (files.size() > 300) files.resize(300);

    savedGames_.clear();
    for (const fs::path& path : files) {
        const auto rec = parsePgn(readFile(path));
        if (!rec) continue;
        SavedGame g;
        g.path = path;
        g.white = rec->tag("White");
        g.black = rec->tag("Black");
        g.result = rec->tag("Result");
        g.date = displayDate(path, rec->tag("Date"));
        g.moves = static_cast<int>(rec->moves.size() + 1) / 2;
        if (const OpeningMatch m = identifyOpening(rec->moves); m.opening && rec->tag("FEN").empty())
            g.opening = m.opening->name;
        const bool youWhite = g.white == "Du", youBlack = g.black == "Du";
        if (youWhite || youBlack) {
            const Color side = youWhite ? Color::White : Color::Black;
            g.accuracyYou = accuracyFromComments(*rec, side);
            g.countsYou = qualityCountsFromComments(*rec, side);
        }        savedGames_.push_back(std::move(g));
    }
}

bool GuiApp::openPgn(const std::string& text, const fs::path& path) {
    const auto rec = parsePgn(text);
    if (!rec || rec->moves.empty()) {
        setStatus(tr("Keine Partie gefunden"));
        return false;
    }
    try {
        Game g = gameFromPgn(*rec);
        // Zu zweit und ohne Uhr starten, ohne die Auswahl fuer neue Partien zu verlieren
        const int keepProfile = selectedProfile_, keepPreset = tcPreset_;
        selectedProfile_ = -1;
        tcPreset_ = 0;
        startNewGame();
        selectedProfile_ = keepProfile;
        tcPreset_ = keepPreset;

        game_ = std::move(g);
        ++generation_;
        pluginGame_ = false;
        gamePlugins_.clear();
        endSoundGen_ = generation_;  // kein Schlussakkord beim Oeffnen
        loadedTags_ = rec->tags;
        savePath_ = path;
        savedGen_ = generation_;  // nicht sofort neu speichern
        ratedGen_ = generation_;  // nie werten
        resultDismissed_ = true;
        flipped_ = rec->tag("Black") == "Du";
        mode_ = Mode::Analysis;
        setStatus(path.empty() ? std::string(tr("Partie aus der Zwischenablage geöffnet"))
                               : tr("Geöffnet: ") + path.filename().string());
        return true;
    } catch (const std::exception& e) {
        setStatus(std::string(tr("Partie nicht lesbar: ")) + e.what());
        return false;
    }
}

void GuiApp::openSavedGame(const fs::path& path) { openPgn(readFile(path), path); }

void GuiApp::tabGames() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    if (ImGui::GetTime() - gamesScannedAt_ > 4.0) scanSavedGames();  // neue Dateien tauchen von selbst auf

    ImGui::BeginChild("##games", ImVec2(0, 0), ImGuiChildFlags_None);

    // --- Liste der gespeicherten Partien ----------------------------------------------
    sectionTitle(tr("Gespeichert"));
    {
        const float btn = 22.0f * s + 2 * ImGui::GetStyle().FramePadding.y;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 2 * btn - 4 * s);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (iconButton("##paste", icon::upload, tr("PGN aus der Zwischenablage öffnen"))) {
            if (char* clip = SDL_GetClipboardText()) {
                openPgn(clip, {});
                SDL_free(clip);
            }
        }
        ImGui::SameLine(0, 4 * s);
        if (iconButton("##folder", icon::folder, tr("Ordner im Explorer öffnen"))) {
            const std::string url = "file:///" + env::gamesDir().generic_string();
            SDL_OpenURL(url.c_str());
        }
        ImGui::PopStyleColor();
    }

    // Suche (Name, Eroeffnung, Datum) und Ergebnis-Filter aus deiner Sicht
    std::vector<size_t> shown;
    {
        if (savedGames_.size() > 3) {
            const float chipW = 34 * s;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 4 * (chipW + 2 * s) - 6 * s);
            ImGui::InputTextWithHint("##gfilter", tr("Suchen: Gegner, Eröffnung, Datum"), gamesFilter_, sizeof(gamesFilter_));
            const char* chips[4] = {tr("Alle"), "S", "R", "N"};
            const char* tips[4] = {tr("Alle Partien"), tr("Nur Siege"), tr("Nur Remis"), tr("Nur Niederlagen")};
            for (int i = 0; i < 4; ++i) {
                ImGui::SameLine(0, i == 0 ? 6 * s : 2 * s);
                const bool sel = gamesResultFilter_ == i;
                ImGui::PushStyleColor(ImGuiCol_Button, sel ? alpha(c.accent, 0.22f) : alpha(c.surfaceRaised, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_Text, sel ? c.accent : c.textMuted);
                if (ImGui::Button((std::string(chips[i]) + "##rf" + std::to_string(i)).c_str(), ImVec2(chipW, 0)))
                    gamesResultFilter_ = i;
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tips[i]);
            }
        }
        std::string q = gamesFilter_;
        for (char& ch : q) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        for (size_t i = 0; i < savedGames_.size(); ++i) {
            const SavedGame& g = savedGames_[i];
            if (!q.empty()) {
                std::string hay = g.white + " " + g.black + " " + displayPlayer(g.white) + " " + displayPlayer(g.black) + " " + g.opening + " " + localizedOpeningName(g.opening, true) + " " + g.date;
                for (char& ch : hay) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (hay.find(q) == std::string::npos) continue;
            }
            if (gamesResultFilter_ != 0) {
                const bool w = g.white == "Du", b = g.black == "Du";
                const int r = g.result == "1-0" ? (w ? 1 : b ? -1 : 9) : g.result == "0-1" ? (b ? 1 : w ? -1 : 9)
                              : g.result == "1/2-1/2" && (w || b) ? 0 : 9;
                const int want = gamesResultFilter_ == 1 ? 1 : gamesResultFilter_ == 2 ? 0 : -1;
                if (r != want) continue;
            }
            shown.push_back(i);
        }
    }

    // So hoch wie noetig, hoechstens gut die Haelfte: darunter bleibt Platz fuer den Export
    const float rowsH = std::max(1.0f, static_cast<float>(shown.size())) * (58 * s + ImGui::GetStyle().ItemSpacing.y);
    const float listH = std::min(std::max(110 * s, rowsH + 4 * s), std::max(160 * s, ImGui::GetContentRegionAvail().y * 0.52f));
    ImGui::BeginChild("##gamelist", ImVec2(0, listH), ImGuiChildFlags_None);
    if (savedGames_.empty()) {
        ImGui::Dummy(ImVec2(0, 8 * s));
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(c.textMuted, "%s",
                           tr("Noch keine Partien. Jede beendete Partie wird hier automatisch abgelegt, damit du sie später nachspielen und analysieren kannst."));
        ImGui::PopTextWrapPos();
    }
    ImFont* hf = theme_.heading ? theme_.heading : ImGui::GetFont();
    ImFont* sf = theme_.strong ? theme_.strong : ImGui::GetFont();
    ImFont* f = ImGui::GetFont();
    if (!savedGames_.empty() && shown.empty()) ImGui::TextColored(c.textMuted, tr("Keine Partie passt zur Suche."));
    for (size_t i : shown) {
        const SavedGame& g = savedGames_[i];
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = 58 * s, pad = 8 * s;
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::PushID(static_cast<int>(i));
        const bool clicked = ImGui::InvisibleButton("##g", ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p1(p0.x + w, p0.y + h);
        const bool open = !savePath_.empty() && savePath_ == g.path;
        if (open) {
            dl->AddRectFilled(p0, p1, Theme::u32(alpha(c.accent, 0.10f)), 8 * s);
            dl->AddRectFilled(ImVec2(p0.x, p0.y + 10 * s), ImVec2(p0.x + 3 * s, p1.y - 10 * s), Theme::u32(c.accent),
                              2 * s);
        } else if (hovered) {
            dl->AddRectFilled(p0, p1, Theme::u32(alpha(c.surfaceRaised, 0.7f)), 8 * s);
        }

        // Ergebnis-Kachel: aus deiner Sicht gefaerbt, wenn du mitgespielt hast
        const bool youWhite = g.white == "Du", youBlack = g.black == "Du";
        ImVec4 tone = c.textMuted;
        std::string res = g.result == "1-0" ? "1-0" : g.result == "0-1" ? "0-1" : g.result == "1/2-1/2" ? "½" : "–";
        if ((youWhite && g.result == "1-0") || (youBlack && g.result == "0-1")) tone = c.qBest;
        if ((youWhite && g.result == "0-1") || (youBlack && g.result == "1-0")) tone = c.qBlunder;
        const float tile = h - 2 * pad;
        const ImVec2 t0(p0.x + pad, p0.y + pad), t1(t0.x + tile + 10 * s, t0.y + tile);
        dl->AddRectFilled(t0, t1, Theme::u32(alpha(tone, 0.16f)), 6 * s);
        const ImVec2 rsz = hf->CalcTextSizeA(18 * s, FLT_MAX, 0.0f, res.c_str());
        dl->AddText(hf, 18 * s, ImVec2((t0.x + t1.x - rsz.x) * 0.5f, (t0.y + t1.y - rsz.y) * 0.5f), Theme::u32(tone),
                    res.c_str());

        const float x = t1.x + 12 * s;
        const std::string names = (g.white.empty() ? "?" : displayPlayer(g.white)) + "  –  " +
                                  (g.black.empty() ? "?" : displayPlayer(g.black));
        dl->PushClipRect(ImVec2(x, p0.y), ImVec2(p1.x - 30 * s, p1.y), true);
        dl->AddText(sf, 16.5f * s, ImVec2(x, p0.y + pad + 1 * s), Theme::u32(c.text), names.c_str());
        std::string meta = g.date + "   " + std::to_string(g.moves) + (g.moves == 1 ? tr(" Zug") : tr(" Züge"));
        if (!g.opening.empty()) meta += "   " + localizedOpeningName(g.opening, english());
        dl->AddText(f, 14.5f * s, ImVec2(x, p0.y + pad + 22 * s), Theme::u32(c.textMuted), meta.c_str());
        dl->PopClipRect();
        if (hovered || open)
            Theme::glyphCentered(dl, theme_.body, 20 * s, ImVec2(p1.x - 16 * s, (p0.y + p1.y) * 0.5f),
                                 Theme::u32(open ? c.accent : c.textMuted), icon::next);
        if (hovered) ImGui::SetTooltip(tr("Öffnen und analysieren"));
        if (clicked) openSavedGame(g.path);
    }
    ImGui::EndChild();

    // --- Diese Partie ---------------------------------------------------------------
    tabExport();
    ImGui::EndChild();
}

void GuiApp::tabExport() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    sectionTitle(tr("Diese Partie als PGN"));
    std::string pgn = pgnText();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, alpha(c.surfaceRaised, 0.6f));
    ImGui::InputTextMultiline("##pgn", pgn.data(), pgn.size() + 1, ImVec2(-FLT_MIN, 96 * s),
                              ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor();

    auto ghost = [&](const Icon& i, const char* label, const char* tip) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alpha(c.accent, 0.16f));
        const bool clicked = ImGui::Button((std::string(i) + "  " + label).c_str());
        ImGui::PopStyleColor(2);
        if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        return clicked;
    };
    if (ghost(icon::copy, tr("Kopieren"), tr("Für lichess.org/paste oder andere Programme"))) {
        ImGui::SetClipboardText(pgn.c_str());
        setStatus(tr("PGN in die Zwischenablage kopiert"));
    }
    ImGui::SameLine(0, 4 * s);
    if (ghost(icon::save, tr("Speichern"), nullptr)) savePgn(false);

    sectionTitle(tr("Stellung (FEN)"));
    std::string fen = toFen(viewedPosition());
    const float btn = 22.0f * s + 2 * ImGui::GetStyle().FramePadding.y;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btn - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputText("##fen", fen.data(), fen.size() + 1, ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (iconButton("##fencopy", icon::copy, tr("FEN der angezeigten Stellung kopieren"))) {
        ImGui::SetClipboardText(fen.c_str());
        setStatus(tr("FEN kopiert"));
    }
    ImGui::PopStyleColor();

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btn - ImGui::GetStyle().ItemSpacing.x);
    const bool enter = ImGui::InputTextWithHint("##fenin", tr("FEN einfügen, um von dieser Stellung zu spielen"), fenInput_,
                                                sizeof(fenInput_), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if ((iconButton("##fenload", icon::start, tr("Stellung laden (Enter)"), false, 0, fenInput_[0] != 0) || enter) &&
        fenInput_[0])
        loadFen(fenInput_);
    ImGui::PopStyleColor();
}

}  // namespace chess::ui
