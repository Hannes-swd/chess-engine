#include "ui/settings.hpp"

#include "util/json.hpp"

#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace chess::ui {

namespace {

// Name in der Datei -> Feld. Eine Liste, damit Lesen und Schreiben nie auseinanderlaufen.
std::vector<std::pair<const char*, bool AssistSettings::*>> fields() {
    return {
        {"legalMoves", &AssistSettings::legalMoves},
        {"dangerTargets", &AssistSettings::dangerTargets},
        {"targetFrame", &AssistSettings::targetFrame},
        {"hangingPieces", &AssistSettings::hangingPieces},
        {"checkGlow", &AssistSettings::checkGlow},
        {"lastMove", &AssistSettings::lastMove},
        {"material", &AssistSettings::material},
        {"hintButton", &AssistSettings::hintButton},
        {"undo", &AssistSettings::undo},
        {"botInfo", &AssistSettings::botInfo},
        {"annotations", &AssistSettings::annotations},
        {"evalBar", &AssistSettings::evalBar},
        {"bestArrow", &AssistSettings::bestArrow},
        {"moveMarks", &AssistSettings::moveMarks},
        {"coordinates", &AssistSettings::coordinates},
        {"darkTheme", &AssistSettings::darkTheme},
        {"sidebarExpanded", &AssistSettings::sidebarExpanded},
        {"autoAnalyse", &AssistSettings::autoAnalyse},
        {"animations", &AssistSettings::animations},
        {"sounds", &AssistSettings::sounds},
        {"premoves", &AssistSettings::premoves},
    };
}

}  // namespace

AssistSettings AssistSettings::learning() {
    AssistSettings s;
    s.hangingPieces = true;
    return s;
}

AssistSettings AssistSettings::standard() { return AssistSettings{}; }

AssistSettings AssistSettings::none() {
    AssistSettings s;
    s.legalMoves = false;
    s.dangerTargets = false;
    s.targetFrame = false;
    s.hangingPieces = false;
    s.checkGlow = false;
    s.material = false;
    s.hintButton = false;
    s.undo = false;
    s.botInfo = false;
    s.evalBar = false;
    s.bestArrow = false;
    s.moveMarks = false;
    // Bleiben an: letzter Zug (zeigt jedes Brett) und eigene Pfeile (das sind nur deine Gedanken)
    return s;
}

bool AssistSettings::sameAssists(const AssistSettings& o) const {
    for (const auto& [name, member] : fields()) {
        const std::string n = name;
        if (n == "coordinates" || n == "darkTheme" || n == "sidebarExpanded" || n == "autoAnalyse" ||
            n == "animations" || n == "sounds" || n == "premoves")
            continue;
        if (this->*member != o.*member) return false;
    }
    return true;
}

std::string AssistSettings::toJson() const {
    std::string s = "{\n";
    const auto f = fields();
    for (size_t i = 0; i < f.size(); ++i)
        s += std::string("  \"") + f[i].first + "\": " + (this->*(f[i].second) ? "true" : "false") + ",\n";
    s += "  \"boardTheme\": " + std::to_string(boardTheme) + ",\n";
    s += "  \"plugins\": [";
    for (size_t i = 0; i < plugins.size(); ++i) {
        std::string id;
        for (char ch : plugins[i]) {
            if (ch == '"' || ch == '\\') id += '\\';
            id += ch;
        }
        s += (i ? ", \"" : "\"") + id + "\"";
    }
    return s + "]\n}\n";
}

bool AssistSettings::fromJson(const std::string& text) {
    const auto v = json::parse(text);
    if (!v || !v->isObject()) return false;
    for (const auto& [name, member] : fields()) this->*member = v->getBool(name, this->*member);
    boardTheme = static_cast<int>(v->getNumber("boardTheme", boardTheme));
    if (const json::Value* list = v->find("plugins"); list && list->isArray()) {
        plugins.clear();
        for (const json::Value& e : list->asArray())
            if (e.isString()) plugins.push_back(e.asString());
    }
    return true;
}

bool AssistSettings::load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    return fromJson(ss.str());
}

bool AssistSettings::save(const std::filesystem::path& file) const {
    std::ofstream out(file, std::ios::binary);
    out << toJson();
    return static_cast<bool>(out);
}

}  // namespace chess::ui
