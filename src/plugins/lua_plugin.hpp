#pragma once

// Lua-Plugins: veraendern alles innerhalb des Spiels – Brettgroesse, Figuren, Startstellung, Zugregeln,
// Siegbedingungen. Jedes Plugin laeuft in einer eigenen Lua-Umgebung ohne Zugriff auf Dateien,
// Netzwerk oder Betriebssystem, mit Speicher- und Rechenzeit-Grenze.
//
// Ein Plugin ist ein Ordner mit plugin.json (Name, Beschreibung ...) und main.lua.
// main.lua gibt eine Tabelle mit Funktionen zurueck (alle optional):
//
//   return {
//     on_load      = function(rules) end,          -- Figuren, Startstellung, Name festlegen
//     on_setup     = function(board) end,          -- Startstellung nachbearbeiten
//     filter_moves = function(board, moves) return moves end,  -- Zuege entfernen/hinzufuegen
//     on_move      = function(board, move) end,    -- nach jedem Zug: Brett beliebig aendern
//     check_end    = function(board, info) return "white"|"black"|"draw", "Grund" end,
//   }
//
// Die vollstaendige Beschreibung steht in docs/PLUGINS.md.

#include "core/game.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct lua_State;

namespace chess::plugins {

// Beschreibung aus plugin.json
struct PluginInfo {
    std::string id;  // Ordnername, eindeutig
    std::string name;
    std::string description;
    std::string author;
    std::string version;
    int priority = 0;  // hoehere Prioritaet greift zuerst
    std::filesystem::path dir;
    std::string error;  // Manifest fehlerhaft o. ae.
};

// Sucht Plugins in allen Ordnern (Unterordner mit main.lua). Spaetere Ordner ersetzen gleiche ids.
std::vector<PluginInfo> discover(const std::vector<std::filesystem::path>& roots);

class LuaPlugin : public std::enable_shared_from_this<LuaPlugin> {
public:
    // Laedt den Quelltext in eine neue, abgeschottete Lua-Umgebung. nullptr bei Fehlern (siehe `error`).
    static std::shared_ptr<LuaPlugin> fromSource(const std::string& source, const std::string& name,
                                                 std::string* error);
    ~LuaPlugin();
    LuaPlugin(const LuaPlugin&) = delete;
    LuaPlugin& operator=(const LuaPlugin&) = delete;

    // on_load ausfuehren (mit veraenderbarer Figurenliste) und die uebrigen Funktionen als Hooks eintragen.
    bool install(Variant& variant, PieceRegistry& registry, int priority, std::string* error);

    const std::string& name() const { return name_; }
    // Ausgaben von print() und Laufzeitfehler (zum Anzeigen in der Oberflaeche)
    std::vector<std::string> log() const;
    int errorCount() const { return errors_; }

    // Grenzen (oeffentlich fuer Tests)
    static constexpr size_t kMemoryLimit = 64u * 1024u * 1024u;
    static constexpr long kInstructionLimit = 50'000'000;

private:
    explicit LuaPlugin(std::string name);
    friend struct LuaAccess;

    bool pushFunction(const char* field);  // Funktion aus der Plugin-Tabelle auf den Stack
    bool hasFunction(const char* field);
    bool pcall(int nargs, int nresults, const char* what);  // Fehler landen im Log
    void customMoves(int ref, const Position& pos, Square from, std::vector<Move>& out);
    void filterMoves(const Position& pos, std::vector<Move>& moves);
    std::optional<GameOutcome> checkEnd(const Game& game);

    std::string name_;
    lua_State* L_ = nullptr;
    mutable std::recursive_mutex mutex_;  // Bots und Analyse rufen Hooks aus anderen Threads auf
    int table_ = -1;                      // Referenz auf die zurueckgegebene Tabelle
    size_t memory_ = 0;
    long instructions_ = 0;
    std::vector<std::string> log_;
    int errors_ = 0;
    // Nur waehrend on_load gesetzt
    Variant* loadVariant_ = nullptr;
    PieceRegistry* loadRegistry_ = nullptr;
};

// Baut Standardschach + die angegebenen Plugins (in Prioritaets-Reihenfolge) zu einer Variante zusammen.
struct VariantBuild {
    Variant variant;
    std::vector<std::shared_ptr<LuaPlugin>> loaded;
    std::vector<std::string> errors;  // "Plugin: Meldung"
};
VariantBuild buildVariant(const std::vector<PluginInfo>& active);

}  // namespace chess::plugins
