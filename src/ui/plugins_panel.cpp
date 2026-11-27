// Bereich "Plugins": Lua-Plugins ein- und ausschalten. Sie gelten ab der naechsten neuen Partie.

#include "ui/gui_app.hpp"

#include "app/environment.hpp"
#include "ui/icons.hpp"

#include <SDL.h>

#include <algorithm>
#include <fstream>

namespace chess::ui {

namespace {

ImVec4 alpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

bool contains(const std::vector<std::string>& list, const std::string& id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

void openFolder(const std::filesystem::path& dir) {
    const std::string url = "file:///" + dir.generic_string();
    SDL_OpenURL(url.c_str());
}

}  // namespace

// Mitgelieferte Beispiele einmalig in den eigenen Plugin-Ordner kopieren, damit dort alles an einem Ort liegt.
// Die Datei ".beispiele" merkt sich, was schon kopiert wurde: Geloeschte Beispiele kommen nicht zurueck,
// eigene Aenderungen werden nie ueberschrieben.
void seedExamplePlugins(const std::filesystem::path& shipped, const std::filesystem::path& own) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(shipped, ec)) return;
    const fs::path marker = own / ".beispiele";
    std::vector<std::string> done;
    {
        std::ifstream in(marker);
        for (std::string line; std::getline(in, line);)
            if (!line.empty()) done.push_back(line);
    }
    bool changed = false;
    for (const auto& e : fs::directory_iterator(shipped, ec)) {
        if (!e.is_directory() || !fs::exists(e.path() / "main.lua")) continue;
        const std::string id = e.path().filename().string();
        if (contains(done, id)) continue;
        if (!fs::exists(own / id))
            fs::copy(e.path(), own / id, fs::copy_options::recursive, ec);
        done.push_back(id);
        changed = true;
    }
    if (changed) {
        std::ofstream out(marker, std::ios::binary);
        for (const std::string& id : done) out << id << "\n";
    }
}

void GuiApp::reloadPlugins() {
    const std::filesystem::path own = env::userPluginsDir();
    if (sandbox_) {
        // Testmodus: nichts kopieren, Beispiele direkt aus dem Programmordner lesen
        plugins_ = plugins::discover({exeDir_ / "plugins", own});
    } else {
        seedExamplePlugins(exeDir_ / "plugins", own);
        plugins_ = plugins::discover({own});  // die Liste ist genau der Inhalt des Ordners
    }
    // Nicht mehr vorhandene Plugins aus der Auswahl entfernen
    std::erase_if(settings_.plugins, [&](const std::string& id) {
        return std::none_of(plugins_.begin(), plugins_.end(), [&](const auto& p) { return p.id == id; });
    });
}

Variant GuiApp::currentVariant() {
    gamePlugins_.clear();
    pluginErrors_.clear();
    pluginGame_ = false;
    // Reihenfolge wie in der Spalte "Aktiv": oben = hoechste Prioritaet
    std::vector<plugins::PluginInfo> active;
    for (const std::string& id : settings_.plugins)
        for (const auto& p : plugins_)
            if (p.id == id) {
                active.push_back(p);
                active.back().priority = 1000 - static_cast<int>(active.size()) * 10;
            }
    if (active.empty()) return standardVariant();

    plugins::VariantBuild b = plugins::buildVariant(active);
    pluginErrors_ = b.errors;
    gamePlugins_ = b.loaded;
    pluginGame_ = !b.loaded.empty();
    if (!b.errors.empty()) setStatus("Plugin-Fehler: " + b.errors.front());
    return std::move(b.variant);
}

void GuiApp::panelPlugins() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    ImGui::BeginChild("##plugins", ImVec2(0, 0), ImGuiChildFlags_None);

    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(c.textMuted, "%s",
                       "Plugins ändern die Regeln: Brettgröße, neue Figuren, Startaufstellung, Siegbedingungen. "
                       "Sie gelten ab der nächsten neuen Partie. Partien mit Plugins werden nicht gewertet.");
    ImGui::PopTextWrapPos();

    // --- Zwei Spalten: verfuegbar | aktiv. Ziehen verschiebt, Reihenfolge in "Aktiv" = Prioritaet ---
    {
        ImGui::Dummy(ImVec2(0, 2 * s));
        const float btn = 22.0f * s + 2 * ImGui::GetStyle().FramePadding.y;
        ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 2 * btn - 4 * s);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (iconButton("##plreload", icon::sync, "Plugins neu einlesen")) {
            reloadPlugins();
            setStatus(std::to_string(plugins_.size()) + " Plugins gefunden");
        }
        ImGui::SameLine(0, 4 * s);
        if (iconButton("##plfolder", icon::folder, "Plugin-Ordner öffnen")) openFolder(env::userPluginsDir());
        ImGui::PopStyleColor();
    }
    if (plugins_.empty()) ImGui::TextColored(c.textMuted, "Der Plugin-Ordner ist leer.");

    bool changed = false;
    const auto find = [&](const std::string& id) -> const plugins::PluginInfo* {
        for (const auto& p : plugins_)
            if (p.id == id) return &p;
        return nullptr;
    };
    std::vector<const plugins::PluginInfo*> available, active;
    for (const std::string& id : settings_.plugins)
        if (const auto* p = find(id)) active.push_back(p);
    for (const auto& p : plugins_)
        if (!contains(settings_.plugins, p.id)) available.push_back(&p);

    const float gap = 10 * s;
    const float colW = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    const float rowGap = 6 * s, pad = 7 * s, grip = 13 * s, fs = 15 * s;
    const ImVec2 colPad(6 * s, 8 * s);
    ImFont* sf = theme_.strong ? theme_.strong : ImGui::GetFont();
    auto rowHeight = [&](const plugins::PluginInfo& p, float w) {
        return sf->CalcTextSizeA(fs, FLT_MAX, w - 2 * pad - grip, p.name.c_str()).y + 2 * pad;
    };
    // Hoehe: so viel, dass beide Spalten ganz sichtbar sind (gleiche Breite wie beim Zeichnen)
    const float innerW = colW - 2 * colPad.x;
    float colH = 0;
    for (const auto* list : {&available, &active}) {
        float h = 0;
        for (const auto* p : *list) h += rowHeight(*p, innerW) + rowGap + ImGui::GetStyle().ItemSpacing.y;
        colH = std::max(colH, h);
    }
    colH = std::max(colH + ImGui::GetTextLineHeightWithSpacing() + 2 * colPad.y + 26 * s,
                    160 * s);

    std::string moveId;  // was in diesem Frame verschoben wurde
    int moveTo = -2;     // -1 = deaktivieren, sonst Position in "Aktiv"

    auto column = [&](const char* id, const char* title, const char* hint, std::vector<const plugins::PluginInfo*>& list,
                      bool isActive) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, alpha(c.surfaceRaised, isActive ? 0.55f : 0.3f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8 * s);
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
            const float h = rowHeight(p, w);
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImGui::PushID(p.id.c_str());
            ImGui::InvisibleButton("##pl", ImVec2(w, h));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                moveId = p.id;
                moveTo = isActive ? -1 : static_cast<int>(active.size());
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
                ImGui::SetDragDropPayload("PLUGIN", p.id.c_str(), p.id.size() + 1);
                ImGui::EndDragDropSource();
            }
            // In "Aktiv" auf eine Zeile fallen lassen: davor einordnen
            if (isActive && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("PLUGIN")) {
                    moveId = static_cast<const char*>(pl->Data);
                    moveTo = static_cast<int>(i);
                }
                ImGui::EndDragDropTarget();
            }
            const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
            if (hovered && !ImGui::GetDragDropPayload() && !p.description.empty()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24);
                ImGui::TextUnformatted(p.description.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, rowGap - ImGui::GetStyle().ItemSpacing.y));

            const ImVec2 p1(p0.x + w, p0.y + h);
            dl->AddRectFilled(p0, p1, Theme::u32(dragging ? alpha(c.accent, 0.25f) : hovered ? c.surfaceRaised : c.surface),
                              6 * s);
            dl->AddRect(p0, p1, Theme::u32(isActive ? alpha(c.accent, 0.45f) : c.border), 6 * s);
            // Griff (sechs Punkte) zeigt: ziehbar
            for (int gy = 0; gy < 3; ++gy)
                for (int gx = 0; gx < 2; ++gx)
                    dl->AddCircleFilled(ImVec2(p0.x + pad - 1 * s + gx * 4 * s, p0.y + h * 0.5f + (gy - 1) * 4.5f * s), 1.2f * s,
                                        Theme::u32(c.textMuted, 0.7f));
            // In "Aktiv" die Rangfolge
            std::string label = p.name;
            if (isActive) label = std::to_string(i + 1) + ". " + label;
            dl->AddText(sf, fs, ImVec2(p0.x + pad + grip, p0.y + pad), Theme::u32(p.error.empty() ? c.text : c.qBlunder),
                        label.c_str(), nullptr, w - 2 * pad - grip);
            // Linie, wo die gezogene Mod landen wuerde
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
        // Ganze Spalte als Ziel: in "Aktiv" ans Ende, in "Verfuegbar" = abschalten
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("PLUGIN")) {
                moveId = static_cast<const char*>(pl->Data);
                moveTo = isActive ? static_cast<int>(active.size()) : -1;
            }
            ImGui::EndDragDropTarget();
        }
    };
    column("##plavail", "Verfügbar", "Alle Plugins sind aktiv.", available, false);
    ImGui::SameLine(0, gap);
    column("##plactive", "Aktiv – oben zuerst", "Plugins hierher ziehen (oder doppelklicken).", active, true);

    if (!moveId.empty() && moveTo != -2) {
        std::vector<std::string>& order = settings_.plugins;
        const auto old = std::find(order.begin(), order.end(), moveId);
        int target = moveTo;
        if (old != order.end()) {
            const int oldIndex = static_cast<int>(old - order.begin());
            order.erase(old);
            if (target > oldIndex) --target;  // beim Verschieben nach unten rueckt alles eins hoch
        }
        if (target >= 0) order.insert(order.begin() + std::clamp(target, 0, static_cast<int>(order.size())), moveId);
        changed = true;
    }
    ImGui::PushTextWrapPos(0);
    ImGui::PushFont(nullptr, 14.5f);
    ImGui::TextColored(c.textMuted, "%s",
                       "Ziehen zum Ein- und Ausschalten, in „Aktiv“ ziehen zum Sortieren. Wollen zwei Plugins "
                       "dasselbe (z. B. die Startstellung), gewinnt das obere.");
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    for (const auto& p : plugins_)
        if (!p.error.empty()) ImGui::TextColored(c.qBlunder, "%s: %s", p.name.c_str(), p.error.c_str());
    if (changed) saveSettings();
    // --- Neue Partie mit der Auswahl ----------------------------------------------------
    ImGui::Dummy(ImVec2(0, 8 * s));
    ImGui::PushFont(theme_.strong, 0.0f);
    const std::string label = std::string(icon::start) + "  " +
                              (settings_.plugins.empty() ? "Neue Partie (normales Schach)" : "Neue Partie mit Plugins");
    if (primaryButton(label.c_str(), ImVec2(-FLT_MIN, 46 * s))) {
        startNewGame();
        mode_ = Mode::Play;
        playTab_ = 1;
    }
    ImGui::PopFont();

    // --- Laufende Partie: Variante, Fehler, Ausgaben -----------------------------------
    sectionTitle("Laufende Partie");
    ImGui::TextUnformatted(game_.variant().name.c_str());
    for (const std::string& e : pluginErrors_) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(c.qBlunder, "%s", e.c_str());
        ImGui::PopTextWrapPos();
    }
    for (const auto& lp : gamePlugins_) {
        const auto log = lp->log();
        if (log.empty()) continue;
        if (ImGui::TreeNode(lp.get(), "%s: %d Meldungen", lp->name().c_str(), static_cast<int>(log.size()))) {
            const size_t from = log.size() > 40 ? log.size() - 40 : 0;
            for (size_t i = from; i < log.size(); ++i) {
                ImGui::PushTextWrapPos(0);
                ImGui::TextColored(c.textMuted, "%s", log[i].c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::TreePop();
        }
    }

    sectionTitle("Eigene Plugins schreiben");
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(c.textMuted, "%s",
                       "Die Liste zeigt genau, was im Plugin-Ordner liegt (Dokumente\\ChessEngine\\Plugins). "
                       "Neuen Ordner mit plugin.json und main.lua hineinlegen, dann oben auf Neu einlesen. "
                       "Die Beispiele dort sind kurze Vorlagen zum Abschauen und Ändern.");
    ImGui::PopTextWrapPos();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::Button((std::string(icon::folder) + "  Plugin-Ordner").c_str())) openFolder(env::userPluginsDir());
    ImGui::SameLine();
    if (ImGui::Button((std::string(icon::book) + "  Anleitung öffnen").c_str())) {
        auto doc = exeDir_ / "docs" / "PLUGINS.md";
        if (!std::filesystem::exists(doc)) doc = exeDir_ / ".." / ".." / "docs" / "PLUGINS.md";
        openFolder(std::filesystem::weakly_canonical(doc));
    }
    ImGui::PopStyleColor();

    ImGui::EndChild();
}

}  // namespace chess::ui
