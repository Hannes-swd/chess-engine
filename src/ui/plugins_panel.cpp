// Bereich "Plugins": Lua-Plugins ein- und ausschalten. Sie gelten ab der naechsten neuen Partie.

#include "ui/gui_app.hpp"

#include "app/environment.hpp"
#include "ui/icons.hpp"

#include <SDL.h>

#include <algorithm>
#include <fstream>

namespace chess::ui {

namespace {

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
    std::vector<plugins::PluginInfo> active;
    for (const auto& p : plugins_)
        if (contains(settings_.plugins, p.id)) active.push_back(p);
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

    sectionTitle("Verfügbar");
    {
        const float btn = 22.0f * s + 2 * ImGui::GetStyle().FramePadding.y;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 2 * btn - 4 * s);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (iconButton("##plreload", icon::sync, "Plugins neu einlesen")) {
            reloadPlugins();
            setStatus(std::to_string(plugins_.size()) + " Plugins gefunden");
        }
        ImGui::SameLine(0, 4 * s);
        if (iconButton("##plfolder", icon::folder, "Ordner für eigene Plugins öffnen")) openFolder(env::userPluginsDir());
        ImGui::PopStyleColor();
    }

    if (plugins_.empty()) ImGui::TextColored(c.textMuted, "Der Plugin-Ordner ist leer.");
    bool changed = false;
    for (const auto& p : plugins_) {
        bool on = contains(settings_.plugins, p.id);
        std::string help;
        if (!p.author.empty() || !p.version.empty())
            help += std::string(p.version.empty() ? "" : "Version " + p.version) +
                    (p.author.empty() ? "" : (p.version.empty() ? "von " : ", von ") + p.author);
        ImGui::PushID(p.id.c_str());
        if (toggleRow(p.name.c_str(), &on, help.c_str())) {
            if (on)
                settings_.plugins.push_back(p.id);
            else
                std::erase(settings_.plugins, p.id);
            changed = true;
        }
        ImGui::PopID();
        // Beschreibung sichtbar unter dem Namen, nicht nur als Tooltip
        if (!p.description.empty()) {
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 8 * s, ImGui::GetCursorPosY() - 10 * s));
            ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - 60 * s);
            ImGui::PushFont(nullptr, 15.0f);
            ImGui::TextColored(c.textMuted, "%s", p.description.c_str());
            ImGui::PopFont();
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, 4 * s));
        }
        if (!p.error.empty()) ImGui::TextColored(c.qBlunder, "%s", p.error.c_str());
    }
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
