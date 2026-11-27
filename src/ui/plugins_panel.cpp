// Bereich "Plugins": rechts zwei Spalten (verfuegbar | aktiv) mit Icon-Kacheln zum Ziehen,
// in der Mitte die Detailseite des gewaehlten Plugins mit Vorschau und dem, was es aendert.

#include "ui/gui_app.hpp"
#include "ui/i18n.hpp"

#include "app/environment.hpp"
#include "core/fen.hpp"
#include "ui/icons.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace chess::ui {

namespace fs = std::filesystem;

namespace {

ImVec4 alpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

bool contains(const std::vector<std::string>& list, const std::string& id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

void openFolder(const fs::path& dir) {
    const std::string url = "file:///" + dir.generic_string();
    SDL_OpenURL(url.c_str());
}

// Fingerabdruck eines Plugin-Ordners (alle Dateien, Namen + Inhalt), FNV-1a 64
std::string folderHash(const fs::path& dir) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec))
        if (e.is_regular_file()) files.push_back(e.path());
    std::sort(files.begin(), files.end());
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](unsigned char b) {
        h ^= b;
        h *= 1099511628211ull;
    };
    for (const fs::path& f : files) {
        for (char ch : fs::relative(f, dir, ec).generic_string()) mix(static_cast<unsigned char>(ch));
        std::ifstream in(f, std::ios::binary);
        for (std::istreambuf_iterator<char> it(in), end; it != end; ++it) mix(static_cast<unsigned char>(*it));
    }
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

// Mitgelieferte Beispiele in den eigenen Plugin-Ordner bringen. ".beispiele" merkt sich je Beispiel den
// Fingerabdruck der kopierten Fassung:
//  - noch nie kopiert            -> kopieren
//  - vom Spieler geloescht       -> nicht wieder anlegen
//  - unveraendert, neue Fassung  -> aktualisieren
//  - vom Spieler veraendert      -> nie anfassen (nur neue Dateien wie ein Icon ergaenzen)
void seedExamplePlugins(const fs::path& shipped, const fs::path& own) {
    std::error_code ec;
    if (!fs::is_directory(shipped, ec)) return;
    const fs::path marker = own / ".beispiele";
    std::vector<std::pair<std::string, std::string>> done;  // id, Fingerabdruck (leer = unbekannt)
    {
        std::ifstream in(marker);
        for (std::string line; std::getline(in, line);) {
            if (line.empty()) continue;
            const size_t tab = line.find('\t');
            done.emplace_back(line.substr(0, tab), tab == std::string::npos ? std::string() : line.substr(tab + 1));
        }
    }
    bool changed = false;
    for (const auto& e : fs::directory_iterator(shipped, ec)) {
        if (!e.is_directory() || !fs::exists(e.path() / "main.lua")) continue;
        const std::string id = e.path().filename().string();
        const fs::path mine = own / id;
        const std::string shipHash = folderHash(e.path());
        auto it = std::find_if(done.begin(), done.end(), [&](const auto& d) { return d.first == id; });
        if (it == done.end()) {
            if (!fs::exists(mine)) fs::copy(e.path(), mine, fs::copy_options::recursive, ec);
            done.emplace_back(id, folderHash(mine));
            changed = true;
            continue;
        }
        if (!fs::exists(mine) || it->second == shipHash) continue;  // geloescht oder schon aktuell
        if (!it->second.empty() && folderHash(mine) == it->second) {
            // unveraendert seit dem Kopieren: neue Fassung uebernehmen
            fs::copy(e.path(), mine, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            it->second = folderHash(mine);
            changed = true;
        } else {
            // veraendert (oder alte Markierung ohne Fingerabdruck): nur fehlende Dateien ergaenzen
            for (const auto& f : fs::recursive_directory_iterator(e.path(), ec)) {
                const fs::path target = mine / fs::relative(f.path(), e.path(), ec);
                if (f.is_regular_file() && !fs::exists(target)) {
                    fs::create_directories(target.parent_path(), ec);
                    fs::copy_file(f.path(), target, ec);
                    changed = true;
                }
            }
            if (it->second.empty() && folderHash(mine) == shipHash) {
                it->second = shipHash;  // jetzt identisch: kuenftig aktualisierbar
                changed = true;
            }
        }
    }
    if (changed) {
        std::ofstream out(marker, std::ios::binary);
        for (const auto& [id, hash] : done) out << id << (hash.empty() ? "" : "\t" + hash) << "\n";
    }
}

// Farbe eines Plugins: aus plugin.json oder aus dem Namen abgeleitet (gedeckte Toene)
ImVec4 pluginColor(const plugins::PluginInfo& p) {
    if (p.color)
        return ImVec4(((p.color >> 16) & 0xFF) / 255.0f, ((p.color >> 8) & 0xFF) / 255.0f, (p.color & 0xFF) / 255.0f,
                      1.0f);
    static const unsigned palette[] = {0x4E6A7A, 0x6E5470, 0x6F7A45, 0x3F6E6A, 0x7A3E48, 0x3D5277, 0x8A7444};
    unsigned h = 0;
    for (char ch : p.id) h = h * 31 + static_cast<unsigned char>(ch);
    const unsigned c = palette[h % (sizeof(palette) / sizeof(palette[0]))];
    return ImVec4(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, 1.0f);
}

}  // namespace

// ===========================================================================
// Daten
// ===========================================================================

void GuiApp::reloadPlugins() {
    const fs::path own = env::userPluginsDir();
    if (sandbox_) {
        // Testmodus: nichts kopieren, Beispiele direkt aus dem Programmordner lesen
        plugins_ = plugins::discover({own, exeDir_ / "plugins"});  // Programmfassung hat Vorrang
    } else {
        seedExamplePlugins(exeDir_ / "plugins", own);
        plugins_ = plugins::discover({own});  // die Liste ist genau der Inhalt des Ordners
    }
    pluginSummaries_.clear();
    images_.clear();  // geaenderte Icons neu laden
    std::erase_if(settings_.plugins, [&](const std::string& id) {
        return std::none_of(plugins_.begin(), plugins_.end(), [&](const auto& p) { return p.id == id; });
    });
    if (std::none_of(plugins_.begin(), plugins_.end(), [&](const auto& p) { return p.id == pluginSel_; }))
        pluginSel_ = plugins_.empty() ? std::string() : plugins_.front().id;
}

Variant GuiApp::currentVariant() {
    gamePlugins_.clear();
    pluginErrors_.clear();
    pluginGame_ = false;
    pluginPlayerStarts_ = false;
    // Reihenfolge wie in der Spalte "Aktiv": oben = hoechste Prioritaet
    std::vector<plugins::PluginInfo> active;
    for (const std::string& id : settings_.plugins)
        for (const auto& p : plugins_)
            if (p.id == id) {
                active.push_back(p);
                active.back().priority = 1000 - static_cast<int>(active.size()) * 10;
            }
    if (active.empty()) return standardVariant();

    for (const auto& p : active) pluginPlayerStarts_ |= p.playerStarts;
    plugins::VariantBuild b = plugins::buildVariant(active);
    for (const auto& lp : b.loaded) lp->takeUiEvents();  // Probelauf beim Bauen zaehlt nicht
    pluginErrors_ = b.errors;
    gamePlugins_ = b.loaded;
    pluginGame_ = !b.loaded.empty();
    if (!b.errors.empty()) setStatus(tr("Plugin-Fehler: ") + b.errors.front());
    return std::move(b.variant);
}

void GuiApp::applyPluginUi() {
    for (const auto& lp : gamePlugins_) {
        for (const plugins::UiEvent& e : lp->takeUiEvents()) {
            const Position& pos = game_.position();
            auto square = [&](int x, int y) {
                return pos.contains(x - 1, y - 1) ? pos.square(x - 1, y - 1) : NoSquare;
            };
            switch (e.kind) {
                case plugins::UiEvent::Kind::Message: setStatus(lp->name() + ": " + e.text); break;
                case plugins::UiEvent::Kind::Sound: {
                    static const std::pair<const char*, Sound::Kind> names[] = {
                        {"move", Sound::Kind::Move},   {"capture", Sound::Kind::Capture}, {"check", Sound::Kind::Check},
                        {"tick", Sound::Kind::Tick},   {"end", Sound::Kind::End},         {"correct", Sound::Kind::Correct},
                        {"wrong", Sound::Kind::Wrong}};
                    for (const auto& [n, k] : names)
                        if (e.text == n) playSound(k);
                    break;
                }
                case plugins::UiEvent::Kind::Mark:
                    if (const Square sq = square(e.x1, e.y1); sq != NoSquare)
                        board_.addAnnotation(Annotation{sq, NoSquare, e.color});
                    break;
                case plugins::UiEvent::Kind::Arrow: {
                    const Square a = square(e.x1, e.y1), b = square(e.x2, e.y2);
                    if (a != NoSquare && b != NoSquare && a != b) board_.addAnnotation(Annotation{a, b, e.color});
                    break;
                }
            }
        }
    }
}

// Was ein Plugin aendert – aus der gebauten Variante abgelesen, nicht aus der Beschreibung
const GuiApp::PluginSummary& GuiApp::pluginSummary(const plugins::PluginInfo& info) {
    if (auto it = pluginSummaries_.find(info.id); it != pluginSummaries_.end()) return it->second;
    PluginSummary sum;
    const plugins::VariantBuild b = plugins::buildVariant({info});
    if (!b.errors.empty()) sum.error = b.errors.front();
    try {
        Game g(b.variant);
        const Position& start = g.startPosition();
        sum.start = start;
        if (start.width() != 8 || start.height() != 8)
            sum.facts.push_back(tr("Brett ") + std::to_string(start.width()) + " × " + std::to_string(start.height()));
        static const char* const standardIds[] = {"pawn", "knight", "bishop", "rook", "queen", "king"};
        std::string added;
        int addedCount = 0;
        for (const PieceType& t : start.registry().all())
            if (std::none_of(std::begin(standardIds), std::end(standardIds), [&](const char* id) { return t.id == id; })) {
                added += (added.empty() ? "" : ", ") + t.name;
                ++addedCount;
            }
        if (addedCount) sum.facts.push_back((addedCount == 1 ? tr("Neue Figur: ") : tr("Neue Figuren: ")) + added);
        // Zweimal bauen: unterscheiden sich die Startstellungen, startet jede Partie anders
        bool random = false;
        if (const auto again = plugins::buildVariant({info}); again.errors.empty()) {
            Game g2(again.variant);
            random = toFen(g2.startPosition()) != toFen(start);
        }
        if (random)
            sum.facts.push_back(start.fullmoveNumber() > 1 ? tr("Startet mitten in der Partie, jedes Mal in einer anderen Stellung")
                                                           : tr("Jede Partie startet mit einer anderen Aufstellung"));
        else if (start.fullmoveNumber() > 1)
            sum.facts.push_back(tr("Startet mitten in der Partie (Zug ") + std::to_string(start.fullmoveNumber()) + ")");
        else if (toFen(start) != kStandardStartFen)
            sum.facts.push_back(tr("Eigene Startaufstellung"));
        const RuleHooks& h = b.variant.hooks;
        if (!h.filterMoves.empty()) sum.facts.push_back(tr("Ändert, welche Züge erlaubt sind"));
        if (!h.onMovePlayed.empty()) sum.facts.push_back(tr("Verändert das Brett nach jedem Zug"));
        if (!h.checkGameEnd.empty()) sum.facts.push_back(tr("Eigene Sieg- oder Remisbedingung"));
        if (!b.variant.standardEndRules) sum.facts.push_back(tr("Ohne die üblichen Matt- und Patt-Regeln"));
        sum.facts.push_back(usesStandardRules(g) ? tr("Bots spielen und analysieren mit Stockfish")
                                                 : tr("Bots spielen mit der eingebauten Engine"));
    } catch (const std::exception& e) {
        sum.error = e.what();
    }
    return pluginSummaries_[info.id] = std::move(sum);
}

// ===========================================================================
// Bausteine
// ===========================================================================

void GuiApp::drawPluginIcon(ImDrawList* dl, const plugins::PluginInfo& p, ImVec2 p0, float size) {
    const ImVec2 p1(p0.x + size, p0.y + size);
    const float r = size * 0.22f;
    if (const Image* img = images_.get(p.icon, 256)) {
        dl->AddImageRounded(img->id(), p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, r);
        return;
    }
    // Ohne eigenes Icon: Farbflaeche mit Anfangsbuchstabe
    dl->AddRectFilled(p0, p1, Theme::u32(pluginColor(p)), r);
    ImFont* hf = theme_.heading ? theme_.heading : ImGui::GetFont();
    const std::string& shown = p.displayName(english());
    std::string letter = shown.empty() ? "?" : shown.substr(0, 1);
    if (!shown.empty() && (static_cast<unsigned char>(shown[0]) & 0xE0) == 0xC0) letter = shown.substr(0, 2);
    const float fs = size * 0.58f;
    const ImVec2 ts = hf->CalcTextSizeA(fs, FLT_MAX, 0.0f, letter.c_str());
    dl->AddText(hf, fs, ImVec2(p0.x + (size - ts.x) * 0.5f, p0.y + (size - ts.y) * 0.5f), IM_COL32(236, 229, 214, 255),
                letter.c_str());
}

void GuiApp::drawMiniBoard(ImDrawList* dl, const Position& pos, ImVec2 p0, float size) {
    const Palette& c = theme_.p();
    const float cell = std::floor(size / std::max(pos.width(), pos.height()));
    const ImVec2 o(p0.x + (size - cell * pos.width()) * 0.5f, p0.y + (size - cell * pos.height()) * 0.5f);
    for (int y = 0; y < pos.height(); ++y)
        for (int x = 0; x < pos.width(); ++x) {
            const ImVec2 a(o.x + x * cell, o.y + (pos.height() - 1 - y) * cell);
            dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), Theme::u32((x + y) % 2 ? c.boardLight : c.boardDark));
            const Piece& pc = pos.at(pos.square(x, y));
            if (!pc.empty())
                BoardView::drawPiece(dl, theme_, pos.registry().get(pc.type), pc.color,
                                     ImVec2(a.x + cell * 0.5f, a.y + cell * 0.5f), cell);
        }
}

// ===========================================================================
// Mitte: Detailseite
// ===========================================================================

void GuiApp::drawPluginDetail() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const plugins::PluginInfo* info = nullptr;
    for (const auto& p : plugins_)
        if (p.id == pluginSel_) info = &p;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* hf = theme_.heading ? theme_.heading : ImGui::GetFont();
    ImFont* sf = theme_.strong ? theme_.strong : ImGui::GetFont();
    if (!info) {
        const char* msg = tr("Wähle rechts ein Plugin aus.");
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(origin.x + (avail.x - ts.x) * 0.5f, origin.y + avail.y * 0.45f), Theme::u32(c.textMuted), msg);
        return;
    }
    const PluginSummary& sum = pluginSummary(*info);

    // Inhalt mittig, nicht breiter als noetig
    const float W = std::min(avail.x - 40 * s, 1040 * s);
    const float x0 = origin.x + (avail.x - W) * 0.5f;
    float y = origin.y + 28 * s;

    // --- Kopf: Icon, Name, Herkunft, Status -------------------------------------------------
    const float iconS = 76 * s;
    drawPluginIcon(dl, *info, ImVec2(x0, y), iconS);
    const float tx = x0 + iconS + 22 * s;
    dl->AddText(hf, 34 * s, ImVec2(tx, y + 2 * s), Theme::u32(c.text), info->displayName(english()).c_str());
    std::string meta;
    if (!info->version.empty()) meta += tr("Version ") + info->version;
    if (!info->author.empty()) meta += (meta.empty() ? tr("von ") : tr(", von ")) + info->author;
    if (meta.empty()) meta = info->id;
    dl->AddText(ImGui::GetFont(), 16 * s, ImVec2(tx, y + 44 * s), Theme::u32(c.textMuted), meta.c_str());
    const auto rank = std::find(settings_.plugins.begin(), settings_.plugins.end(), info->id);
    const bool active = rank != settings_.plugins.end();
    {
        const std::string chip = active ? tr("Aktiv, Platz ") + std::to_string(rank - settings_.plugins.begin() + 1) + tr(" von ") +
                                              std::to_string(settings_.plugins.size())
                                        : tr("Nicht aktiv");
        const ImVec2 cs = sf->CalcTextSizeA(14.5f * s, FLT_MAX, 0.0f, chip.c_str());
        const float mw = ImGui::GetFont()->CalcTextSizeA(16 * s, FLT_MAX, 0.0f, meta.c_str()).x;
        const ImVec2 a(tx + mw + 14 * s, y + 42 * s), b(a.x + cs.x + 18 * s, a.y + cs.y + 6 * s);
        dl->AddRectFilled(a, b, Theme::u32(active ? alpha(c.accent, 0.2f) : alpha(c.surfaceRaised, 1.0f)), (b.y - a.y) * 0.5f);
        dl->AddText(sf, 14.5f * s, ImVec2(a.x + 9 * s, a.y + 3 * s), Theme::u32(active ? c.accent : c.textMuted), chip.c_str());
    }
    y += iconS + 30 * s;

    // --- Koerper: Vorschau links, Text rechts ------------------------------------------------
    const float bottomSpace = 24 * s;
    const float prevS = std::max(160 * s, std::min({avail.y - (y - origin.y) - bottomSpace, W * 0.5f, 560 * s}));
    const ImVec2 pv0(x0, y);
    if (const Image* img = images_.get(info->preview, 1024)) {
        // eigenes Vorschaubild, eingepasst
        const float f = std::min(prevS / img->width, prevS / img->height);
        const ImVec2 sz(img->width * f, img->height * f);
        dl->AddImageRounded(img->id(), pv0, ImVec2(pv0.x + sz.x, pv0.y + sz.y), ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, 8 * s);
    } else if (sum.start) {
        dl->AddRectFilled(ImVec2(pv0.x - 6 * s, pv0.y - 6 * s), ImVec2(pv0.x + prevS + 6 * s, pv0.y + prevS + 6 * s),
                          Theme::u32(pluginColor(*info)), 10 * s);
        drawMiniBoard(dl, *sum.start, pv0, prevS);
    }

    const float rx = x0 + prevS + 40 * s;
    const float rw = std::max(200 * s, x0 + W - rx);
    ImGui::SetCursorScreenPos(ImVec2(rx, y - 4 * s));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + rw);
    if (!info->displayDescription(english()).empty()) {
        ImGui::PushFont(nullptr, 18.0f);
        ImGui::TextUnformatted(info->displayDescription(english()).c_str());
        ImGui::PopFont();
    }
    if (!sum.error.empty() || !info->error.empty()) {
        ImGui::Dummy(ImVec2(0, 6 * s));
        ImGui::TextColored(c.qBlunder, "%s", (info->error.empty() ? sum.error : info->error).c_str());
    }
    if (!sum.facts.empty()) {
        sectionTitle(tr("Was es ändert"));
        for (const std::string& f : sum.facts) {
            const ImVec2 lp = ImGui::GetCursorScreenPos();
            Theme::glyphCentered(dl, theme_.body, 18 * s, ImVec2(lp.x + 8 * s, lp.y + ImGui::GetTextLineHeight() * 0.5f),
                                 Theme::u32(pluginColor(*info)), icon::checkPlain);
            ImGui::SetCursorScreenPos(ImVec2(lp.x + 24 * s, lp.y));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + rw - 24 * s);
            ImGui::TextUnformatted(f.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::Dummy(ImVec2(0, 14 * s));
    ImGui::PushFont(theme_.strong, 0.0f);
    if (!active) {
        if (primaryButton((std::string(icon::add) + tr("  Aktivieren")).c_str(), ImVec2(0, 42 * s))) {
            settings_.plugins.push_back(info->id);
            saveSettings();
        }
    } else {
        if (primaryButton((std::string(icon::start) + tr("  Neue Partie damit")).c_str(), ImVec2(0, 42 * s))) {
            startNewGame();
            mode_ = Mode::Play;
            playTab_ = 1;
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(icon::close) + tr("  Deaktivieren")).c_str(), ImVec2(0, 42 * s))) {
            std::erase(settings_.plugins, info->id);
            saveSettings();
        }
    }
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::Button((std::string(icon::folder) + tr("  Ordner des Plugins")).c_str())) openFolder(info->dir);
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    if (sum.start && !images_.get(info->preview, 1024)) {
        const char* cap = tr("Startstellung");
        dl->AddText(ImGui::GetFont(), 14 * s, ImVec2(pv0.x, pv0.y + prevS + 12 * s), Theme::u32(c.textMuted), cap);
    }
}

// ===========================================================================
// Rechts: Spalten mit Kacheln
// ===========================================================================

void GuiApp::panelPlugins() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    if (pluginSel_.empty() && !plugins_.empty()) pluginSel_ = plugins_.front().id;
    ImGui::BeginChild("##plugins", ImVec2(0, 0), ImGuiChildFlags_None);

    {
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - 2 * (22.0f * s + 2 * ImGui::GetStyle().FramePadding.y) - 16 * s);
        ImGui::TextColored(c.textMuted, "%s", tr("Ziehen zum Aktivieren und Sortieren. Klick zeigt Details."));
        ImGui::PopTextWrapPos();
        const float btn = 22.0f * s + 2 * ImGui::GetStyle().FramePadding.y;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 2 * btn - 4 * s);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (iconButton("##plreload", icon::sync, tr("Plugins neu einlesen"))) {
            reloadPlugins();
            setStatus(std::to_string(plugins_.size()) + tr(" Plugins gefunden"));
        }
        ImGui::SameLine(0, 4 * s);
        if (iconButton("##plfolder", icon::folder, tr("Plugin-Ordner öffnen"))) openFolder(env::userPluginsDir());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, 4 * s));
    if (plugins_.empty()) ImGui::TextColored(c.textMuted, tr("Der Plugin-Ordner ist leer."));

    std::vector<const plugins::PluginInfo*> available, active;
    for (const std::string& id : settings_.plugins)
        for (const auto& p : plugins_)
            if (p.id == id) active.push_back(&p);
    for (const auto& p : plugins_)
        if (!contains(settings_.plugins, p.id)) available.push_back(&p);

    const float gap = 10 * s;
    const float colW = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    const ImVec2 colPad(6 * s, 8 * s);
    const float rowH = 44 * s, rowGap = 6 * s, iconS = 30 * s, fs = 15 * s;
    ImFont* sf = theme_.strong ? theme_.strong : ImGui::GetFont();
    const size_t maxRows = std::max(available.size(), active.size());
    const float colH = std::max(150 * s, maxRows * (rowH + rowGap + ImGui::GetStyle().ItemSpacing.y) + ImGui::GetTextLineHeightWithSpacing() +
                                             2 * colPad.y + 30 * s);

    std::string moveId;
    int moveTo = -2;  // -1 = deaktivieren, sonst Position in "Aktiv"

    auto column = [&](const char* id, const char* title, const char* hint, std::vector<const plugins::PluginInfo*>& list,
                      bool isActive) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, alpha(c.surfaceRaised, isActive ? 0.55f : 0.3f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10 * s);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, colPad);
        ImGui::BeginChild(id, ImVec2(colW, colH), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PushFont(theme_.strong, 15.0f);
        ImGui::TextColored(isActive ? c.accent : c.textMuted, "%s", title);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 2 * s));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = ImGui::GetContentRegionAvail().x;
        for (size_t i = 0; i < list.size(); ++i) {
            const auto& p = *list[i];
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImGui::PushID(p.id.c_str());
            if (ImGui::InvisibleButton("##pl", ImVec2(w, rowH))) pluginSel_ = p.id;
            const bool hovered = ImGui::IsItemHovered();
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                moveId = p.id;
                moveTo = isActive ? -1 : static_cast<int>(active.size());
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("PLUGIN", p.id.c_str(), p.id.size() + 1);
                // Beim Ziehen haengt eine kleine Kachel mit Icon und Name an der Maus
                const ImVec2 tp = ImGui::GetCursorScreenPos();
                drawPluginIcon(ImGui::GetWindowDrawList(), p, tp, 24 * s);
                ImGui::Dummy(ImVec2(24 * s, 24 * s));
                ImGui::SameLine();
                ImGui::TextUnformatted(p.displayName(english()).c_str());
                ImGui::EndDragDropSource();
            }
            if (isActive && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("PLUGIN")) {
                    moveId = static_cast<const char*>(pl->Data);
                    moveTo = static_cast<int>(i);
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, rowGap - ImGui::GetStyle().ItemSpacing.y));

            const bool selected = pluginSel_ == p.id;
            const ImVec2 p1(p0.x + w, p0.y + rowH);
            dl->AddRectFilled(p0, p1, Theme::u32(hovered ? c.surfaceRaised : c.surface), 8 * s);
            if (selected) dl->AddRect(p0, p1, Theme::u32(c.accent), 8 * s, 0, 1.5f * s);
            drawPluginIcon(dl, p, ImVec2(p0.x + 7 * s, p0.y + (rowH - iconS) * 0.5f), iconS);
            const float tx = p0.x + 7 * s + iconS + 9 * s;
            std::string label = p.displayName(english());
            dl->PushClipRect(ImVec2(tx, p0.y), ImVec2(p1.x - 4 * s, p1.y), true);
            const ImVec2 ls = sf->CalcTextSizeA(fs, FLT_MAX, p1.x - tx - 6 * s, label.c_str());
            dl->AddText(sf, fs, ImVec2(tx, p0.y + (rowH - ls.y) * 0.5f), Theme::u32(p.error.empty() ? c.text : c.qBlunder),
                        label.c_str(), nullptr, p1.x - tx - 6 * s);
            dl->PopClipRect();
            // Rang in "Aktiv" als kleine Zahl am Icon
            if (isActive) {
                const std::string n = std::to_string(i + 1);
                const ImVec2 bc(p0.x + 7 * s + iconS, p0.y + (rowH - iconS) * 0.5f);
                dl->AddCircleFilled(bc, 8 * s, Theme::u32(c.accent));
                const ImVec2 ns = sf->CalcTextSizeA(11.5f * s, FLT_MAX, 0.0f, n.c_str());
                dl->AddText(sf, 11.5f * s, ImVec2(bc.x - ns.x * 0.5f, bc.y - ns.y * 0.5f), Theme::u32(c.accentText), n.c_str());
            }
            if (isActive && ImGui::GetDragDropPayload() && ImGui::IsMouseHoveringRect(p0, p1))
                dl->AddLine(ImVec2(p0.x, p0.y - rowGap * 0.5f), ImVec2(p1.x, p0.y - rowGap * 0.5f), Theme::u32(c.accent),
                            2.5f * s);
        }
        if (list.empty()) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(alpha(c.textMuted, 0.8f), "%s", hint);
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("PLUGIN")) {
                moveId = static_cast<const char*>(pl->Data);
                moveTo = isActive ? static_cast<int>(active.size()) : -1;
            }
            ImGui::EndDragDropTarget();
        }
    };
    column("##plavail", tr("Verfügbar"), tr("Alle Plugins sind aktiv."), available, false);
    ImGui::SameLine(0, gap);
    column("##plactive", tr("Aktiv – oben zuerst"), tr("Hierher ziehen oder doppelklicken."), active, true);

    if (!moveId.empty() && moveTo != -2) {
        std::vector<std::string>& order = settings_.plugins;
        const auto old = std::find(order.begin(), order.end(), moveId);
        int target = moveTo;
        if (old != order.end()) {
            const int oldIndex = static_cast<int>(old - order.begin());
            order.erase(old);
            if (target > oldIndex) --target;
        }
        if (target >= 0) order.insert(order.begin() + std::clamp(target, 0, static_cast<int>(order.size())), moveId);
        pluginSel_ = moveId;
        saveSettings();
    }
    ImGui::PushTextWrapPos(0);
    ImGui::PushFont(nullptr, 14.5f);
    ImGui::TextColored(c.textMuted, "%s", tr("Wollen zwei Plugins dasselbe, etwa die Startstellung, gewinnt das obere."));
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    for (const auto& p : plugins_)
        if (!p.error.empty()) ImGui::TextColored(c.qBlunder, "%s: %s", p.displayName(english()).c_str(), p.error.c_str());

    ImGui::Dummy(ImVec2(0, 6 * s));
    ImGui::PushFont(theme_.strong, 0.0f);
    const std::string label = std::string(icon::start) + "  " +
                              (settings_.plugins.empty() ? tr("Neue Partie (normales Schach)") : tr("Neue Partie mit Plugins"));
    if (primaryButton(label.c_str(), ImVec2(-FLT_MIN, 46 * s))) {
        startNewGame();
        mode_ = Mode::Play;
        playTab_ = 1;
    }
    ImGui::PopFont();

    // Laufende Partie nur zeigen, wenn es etwas zu sagen gibt (Fehler, Ausgaben der Plugins)
    bool anyLog = !pluginErrors_.empty();
    for (const auto& lp : gamePlugins_) anyLog |= !lp->log().empty();
    if (anyLog) {
        sectionTitle(tr("Meldungen der laufenden Partie"));
        for (const std::string& e : pluginErrors_) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(c.qBlunder, "%s", e.c_str());
            ImGui::PopTextWrapPos();
        }
        for (const auto& lp : gamePlugins_) {
            const auto log = lp->log();
            if (log.empty()) continue;
            if (ImGui::TreeNode(lp.get(), tr("%s: %d Meldungen"), lp->name().c_str(), static_cast<int>(log.size()))) {
                const size_t from = log.size() > 40 ? log.size() - 40 : 0;
                for (size_t i = from; i < log.size(); ++i) {
                    ImGui::PushTextWrapPos(0);
                    ImGui::TextColored(c.textMuted, "%s", log[i].c_str());
                    ImGui::PopTextWrapPos();
                }
                ImGui::TreePop();
            }
        }
    }

    ImGui::Dummy(ImVec2(0, 4 * s));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::Button((std::string(icon::book) + tr("  Eigene Plugins schreiben")).c_str())) {
        auto doc = exeDir_ / "docs" / "PLUGINS.md";
        if (!fs::exists(doc)) doc = exeDir_ / ".." / ".." / "docs" / "PLUGINS.md";
        openFolder(fs::weakly_canonical(doc));
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Anleitung: Ordner mit plugin.json, main.lua und optional icon.png in den Plugin-Ordner legen"));

    ImGui::EndChild();
}

}  // namespace chess::ui
