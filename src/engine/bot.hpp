#pragma once

#include "core/game.hpp"
#include "engine/engine.hpp"
#include "util/json.hpp"

#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace chess {

enum class EngineChoice { Auto, Stockfish, Builtin };

// Wie stark die Stilvorlieben wirken (in Centipawns bei aggression = 1).
struct StyleWeights {
    double capture = 60;  // Schlagzug
    double check = 40;    // Schachgebot
    double advance = 8;   // pro Reihe nach vorne
};

// Wie schnell ein Bot zieht.
enum class TempoMode { Instant, Fixed, Human };

struct Tempo {
    TempoMode mode = TempoMode::Instant;  // Instant: nur Rechenzeit der Engine
    int minMs = 500;                       // Fixed: immer minMs; Human: zufaellig zwischen min und max
    int maxMs = 4000;
    double variance = 0.45;  // Human: wie stark die Bedenkzeit schwankt (Streuung, log-normal)
};

// Was der Bot ueber die Partie-Situation wissen muss.
struct BotContext {
    int remainingMs = -1;  // eigene Restzeit auf der Uhr, -1 = ohne Uhr
    int incrementMs = 0;
};

// Ein Bot = Engine-Einstellungen + Persoenlichkeit. Wird aus bots/*.json geladen.
struct BotProfile {
    std::string id;  // Dateiname ohne .json
    std::string name;
    std::string description;
    std::string nameEn;         // optional: englischer Name ("name_en")
    std::string descriptionEn;  // optional: englische Beschreibung ("description_en")
    const std::string& displayName(bool english) const { return english && !nameEn.empty() ? nameEn : name; }
    const std::string& displayDescription(bool english) const {
        return english && !descriptionEn.empty() ? descriptionEn : description;
    }
    EngineChoice engine = EngineChoice::Auto;

    // Menschliches Staerke-Modell: Ziel-Elo 400..3000. Bestimmt Suchtiefe, wie oft ein schwaecherer
    // Kandidat gewaehlt wird und wie oft der Bot patzt. 0 = stattdessen die Stockfish-Werte unten.
    int strength = 0;

    int skill = 20;         // Stockfish "Skill Level" 0..20
    int elo = 0;            // > 0: Stockfish UCI_Elo (ab ca. 1320)
    int depth = 12;         // Suchtiefe (0 = egal)
    int movetimeMs = 500;   // Bedenkzeit pro Zug (0 = egal)

    int candidates = 4;     // so viele Kandidatenzuege bewertet die Engine
    int window = 60;        // Kandidaten duerfen hoechstens so viele Centipawns schlechter sein
    double aggression = 0;  // -1 = sehr vorsichtig, 0 = neutral, 1 = sehr aggressiv
    double randomness = 0;  // Zufallsrauschen in Centipawns
    double blunderChance = 0;  // 0..1: Wahrscheinlichkeit fuer einen bewusst zufaelligen Zug
    StyleWeights weights;

    Tempo tempo;
    double pressureBlunder = 0;  // 0..1: zusaetzliche Fehlerneigung, wenn weniger als 10 s auf der Uhr sind
};

// Wie ein Spieler mit einer bestimmten Elo "rechnet":
//  - depth: so viele Halbzuege schaut er voraus (Taktiken dahinter sieht er nicht)
//  - candidates: aus so vielen Zuegen waehlt er
//  - temperature: wie stark er schwaechere Kandidaten in Betracht zieht (Centipawns; hoch = oft daneben)
//  - blunder: Wahrscheinlichkeit fuer einen echten Aussetzer (ein beliebiger Zug)
struct StrengthModel {
    int depth = 1;
    int candidates = 8;
    double temperature = 100;
    double blunder = 0;
};
StrengthModel strengthModel(int elo);

BotProfile defaultBotProfile();
std::optional<BotProfile> parseBotProfile(const json::Value& v, std::string& error);
// Laedt alle *.json im Ordner, alphabetisch. Fehlerhafte Dateien landen in `errors`.
std::vector<BotProfile> loadBotProfiles(const std::filesystem::path& dir, std::vector<std::string>& errors);
// Profil als lesbares JSON (Gegenstueck zu parseBotProfile).
std::string botProfileToJson(const BotProfile& p);
const char* tempoModeName(TempoMode m);  // "Sofort", "Fest", "Wie ein Mensch"

class Bot {
public:
    struct Choice {
        Move move;
        Score score;
        std::string reason;
    };

    explicit Bot(BotProfile profile, uint32_t seed = std::random_device{}());

    const BotProfile& profile() const { return profile_; }
    void setProfile(BotProfile p) { profile_ = std::move(p); }

    // Zug waehlen. Mit Uhr (ctx.remainingMs >= 0) rechnet die Engine kuerzer und in Zeitnot
    // steigt die Fehlerneigung um pressureBlunder.
    Choice choose(const Game& game, Engine& engine, const BotContext& ctx = {});

    // Wie lange der Bot "nachdenken" soll, bevor er zieht (Millisekunden, inklusive Rechenzeit).
    // Menschlich: zufaellig, schneller bei erzwungenen Zuegen, Eroeffnung und Rueckschlaegen,
    // laenger in komplizierten Stellungen und immer passend zur Restzeit.
    int thinkDelayMs(const Game& game, const BotContext& ctx = {});

private:
    Choice chooseHuman(const Position& pos, const std::vector<Move>& legal, const std::vector<ScoredMove>& cands,
                       const StrengthModel& model, const BotContext& ctx);

    BotProfile profile_;
    std::mt19937 rng_;
};

}  // namespace chess
