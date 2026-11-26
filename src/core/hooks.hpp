#pragma once

#include "core/types.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace chess {

class Position;
class Game;

// Eine geordnete Liste von Eingriffen in die Regeln. Hoehere Prioritaet laeuft zuerst,
// bei gleicher Prioritaet gilt die Reihenfolge des Hinzufuegens.
// `owner` ist z. B. der Plugin-Name, damit ein Plugin alle seine Hooks wieder entfernen kann.
template <class Signature>
class HookList {
public:
    struct Entry {
        std::string owner;
        int priority = 0;
        std::function<Signature> fn;
    };

    void add(std::string owner, int priority, std::function<Signature> fn) {
        auto it = std::upper_bound(entries_.begin(), entries_.end(), priority,
                                   [](int p, const Entry& e) { return p > e.priority; });
        entries_.insert(it, Entry{std::move(owner), priority, std::move(fn)});
    }

    void removeOwner(const std::string& owner) {
        std::erase_if(entries_, [&](const Entry& e) { return e.owner == owner; });
    }

    const std::vector<Entry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }

private:
    std::vector<Entry> entries_;
};

// Alle Stellen, an denen ein Plugin in das Spiel eingreifen kann.
struct RuleHooks {
    // Nach dem Laden der Startstellung: Brettgroesse, Figuren, Startaufstellung aendern.
    HookList<void(Position&)> onSetup;
    // Zuege hinzufuegen, entfernen oder veraendern, bevor der Spieler sie sieht.
    HookList<void(const Position&, std::vector<Move>&)> filterMoves;
    // Nach jedem Zug: die Stellung darf beliebig manipuliert werden.
    HookList<void(Position&, const Move&)> onMovePlayed;
    // Eigene Sieg-/Remisbedingungen. Der erste Hook mit Ergebnis gewinnt.
    HookList<std::optional<GameOutcome>(const Game&)> checkGameEnd;

    void removeOwner(const std::string& owner) {
        onSetup.removeOwner(owner);
        filterMoves.removeOwner(owner);
        onMovePlayed.removeOwner(owner);
        checkGameEnd.removeOwner(owner);
    }
};

}  // namespace chess
