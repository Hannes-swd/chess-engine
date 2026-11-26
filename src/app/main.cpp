// Konsolen-Version: Hotseat, Spiel gegen Bots und Partieanalyse.
// Platzhalter, bis die SDL2 + Dear ImGui Oberflaeche (Meilenstein 2) steht.

#include "analysis/analysis.hpp"
#include "app/environment.hpp"
#include "core/fen.hpp"
#include "core/game.hpp"
#include "core/notation.hpp"
#include "core/pgn.hpp"
#include "engine/bot.hpp"
#include "engine/builtin_engine.hpp"
#include "engine/uci_engine.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace chess;
namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// Hilfsfunktionen
// ---------------------------------------------------------------------------

std::string trim(std::string s) {
    if (s.rfind("\xEF\xBB\xBF", 0) == 0) s.erase(0, 3);  // BOM bei umgeleiteter Eingabe
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> words(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

void printBoard(const Position& pos) {
    std::cout << '\n';
    for (int r = pos.height() - 1; r >= 0; --r) {
        std::cout << (r + 1 < 10 ? " " : "") << r + 1 << "  ";
        for (int f = 0; f < pos.width(); ++f) {
            const Piece& p = pos.at(pos.square(f, r));
            char c = '.';
            if (!p.empty()) {
                c = pos.registry().get(p.type).symbol;
                if (p.color == Color::Black) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            std::cout << c << ' ';
        }
        std::cout << '\n';
    }
    std::cout << "\n    ";
    for (int f = 0; f < pos.width(); ++f) std::cout << static_cast<char>('a' + f) << ' ';
    std::cout << "\n\n";
}

void printHelp() {
    std::cout << "Befehle:\n"
                 "  <Zug>                 z. B. e4, Nf3, O-O, exd5, e8=Q oder e2e4\n"
                 "  zuege                 alle legalen Züge anzeigen\n"
                 "  zurueck               letzten Zug zurücknehmen (gegen Bot: bis du wieder dran bist)\n"
                 "  bots                  verfügbare Bots anzeigen\n"
                 "  bot <nr|name> [weiss|schwarz]   gegen einen Bot spielen (Standard: Bot spielt Schwarz)\n"
                 "  bot aus               wieder zu zweit am selben Rechner\n"
                 "  analyse [tiefe]       jeden Zug der Partie bewerten lassen\n"
                 "  engine [pfad]         Engine-Status anzeigen / Stockfish-Pfad setzen\n"
                 "  pgn                   Partie als PGN-Text (mit Bewertungen, falls analysiert)\n"
                 "  fen                   aktuelle Stellung als FEN\n"
                 "  neu                   neue Partie\n"
                 "  hilfe                 diese Hilfe\n"
                 "  ende                  beenden\n";
}

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------

struct App {
    fs::path exeDir = env::executableDir();
    Game game;
    BuiltinEngine builtin;
    std::unique_ptr<UciEngine> stockfish;
    std::vector<BotProfile> profiles;
    std::optional<Bot> bot;
    Color botColor = Color::Black;
    std::optional<GameAnalysis> analysis;
    int analysisPly = -1;

    bool launchStockfish(const fs::path& path) {
        std::string err;
        auto e = UciEngine::launch(path, err);
        if (!e) {
            std::cout << "Stockfish konnte nicht gestartet werden: " << err << "\n";
            return false;
        }
        stockfish = std::move(e);
        std::cout << "Engine geladen: " << stockfish->name() << " (" << path.string() << ")\n";
        return true;
    }

    // Stockfish fuer Standardschach, sonst die eingebaute Engine (versteht alle Varianten).
    Engine& engineFor(EngineChoice choice) {
        if (choice == EngineChoice::Builtin) return builtin;
        if (stockfish && usesStandardRules(game)) return *stockfish;
        return builtin;
    }

    void loadProfiles() {
        std::vector<std::string> errors;
        profiles = loadBotProfiles(exeDir / "bots", errors);
        for (const auto& e : errors) std::cout << "Bot-Profil fehlerhaft: " << e << "\n";
        if (profiles.empty()) profiles.push_back(defaultBotProfile());
    }

    void showEngine() {
        if (stockfish)
            std::cout << "Stockfish: " << stockfish->name() << "\n";
        else
            std::cout << "Stockfish: nicht gefunden. Lege stockfish.exe in den Ordner \"engines\" neben\n"
                         "  chessengine.exe oder ins Projektverzeichnis, oder nutze: engine <pfad zur exe>\n"
                         "  Bis dahin spielt und analysiert die eingebaute Engine (deutlich schwächer).\n";
    }

    void listBots() {
        for (size_t i = 0; i < profiles.size(); ++i) {
            const BotProfile& p = profiles[i];
            std::cout << "  " << i + 1 << ". " << p.name;
            if (p.elo > 0)
                std::cout << " (Elo " << p.elo << ")";
            else
                std::cout << " (Stufe " << p.skill << "/20)";
            if (p.aggression > 0.05) std::cout << " – aggressiv";
            if (p.aggression < -0.05) std::cout << " – vorsichtig";
            std::cout << "\n     " << p.description << "\n";
        }
    }

    const BotProfile* findProfile(const std::string& key) {
        const std::string k = lower(key);
        try {
            const size_t idx = std::stoul(k);
            if (idx >= 1 && idx <= profiles.size()) return &profiles[idx - 1];
        } catch (...) {
        }
        for (const auto& p : profiles)
            if (lower(p.id) == k || lower(p.name).rfind(k, 0) == 0) return &p;
        return nullptr;
    }

    void cmdBot(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            std::cout << "Benutzung: bot <nr|name> [weiss|schwarz]  oder  bot aus\n";
            return;
        }
        if (lower(args[1]) == "aus") {
            bot.reset();
            std::cout << "Bot aus – Hotseat-Modus.\n";
            return;
        }
        const BotProfile* p = findProfile(args[1]);
        if (!p) {
            std::cout << "Unbekannter Bot \"" << args[1] << "\". \"bots\" zeigt die Liste.\n";
            return;
        }
        botColor = Color::Black;
        if (args.size() > 2) {
            const std::string c = lower(args[2]);
            if (c == "weiss" || c == "weiß" || c == "w") botColor = Color::White;
        }
        bot.emplace(*p);
        Engine& e = engineFor(p->engine);
        std::cout << "Gegner: " << p->name << " (" << (botColor == Color::White ? "Weiß" : "Schwarz") << ", Engine: "
                  << e.name() << ")\n";
        if (p->engine == EngineChoice::Stockfish && !stockfish)
            std::cout << "Hinweis: Dieses Profil wünscht Stockfish, der nicht gefunden wurde.\n";
    }

    void botTurn() {
        while (bot && !game.outcome().over() && game.position().sideToMove() == botColor) {
            Engine& e = engineFor(bot->profile().engine);
            const Bot::Choice c = bot->choose(game, e);
            if (!c.move.valid()) break;
            Position tmp = game.position();
            const std::string san = toSan(tmp, c.move);
            if (!game.play(c.move)) break;
            std::cout << bot->profile().name << " spielt " << san << "  (" << c.reason;
            if (c.score.mate || c.score.value != 0) std::cout << ", " << c.score.toString();
            std::cout << ")\n";
        }
    }

    void cmdAnalyse(const std::vector<std::string>& args) {
        if (game.ply() == 0) {
            std::cout << "Noch keine Züge zum Analysieren.\n";
            return;
        }
        Engine& e = engineFor(EngineChoice::Auto);
        SearchLimits lim;
        if (&e == stockfish.get()) {
            lim.depth = 14;
        } else {
            lim.depth = 8;
            lim.movetimeMs = 300;
        }
        if (args.size() > 1) {
            try {
                lim.depth = std::max(1, std::stoi(args[1]));
            } catch (...) {
            }
        }
        std::cout << "Analysiere mit " << e.name() << " (Tiefe " << lim.depth << ") ...\n";
        auto result = analyseGame(game, e, lim, {}, [](int done, int total) {
            std::cout << "\r  Stellung " << done << "/" << total << std::flush;
        });
        std::cout << "\r                              \r";
        std::cout << formatAnalysis(game, result);
        if (result.ok) {
            analysis = std::move(result);
            analysisPly = game.ply();
            std::cout << "\"pgn\" gibt die Partie jetzt mit diesen Bewertungen aus.\n";
        }
    }

    void cmdEngine(const std::vector<std::string>& args, const std::string& rawInput) {
        if (args.size() < 2) {
            showEngine();
            return;
        }
        // Pfad darf Leerzeichen enthalten: alles nach "engine "
        std::string path = trim(rawInput.substr(rawInput.find(' ') + 1));
        if (path.size() >= 2 && path.front() == '"' && path.back() == '"') path = path.substr(1, path.size() - 2);
        launchStockfish(path);
    }

    void printPgn() {
        PgnTags tags;
        if (bot) {
            tags.emplace_back(botColor == Color::White ? "White" : "Black", bot->profile().name);
            tags.emplace_back(botColor == Color::White ? "Black" : "White", "Spieler");
        }
        std::vector<std::string> comments;
        const bool withEval = analysis && analysisPly == game.ply();
        if (withEval) {
            comments = pgnComments(*analysis);
            tags.emplace_back("Annotator", analysis->engineName);
        }
        std::cout << '\n' << toPgn(game, tags, withEval ? &comments : nullptr) << '\n';
    }

    void undo() {
        if (!game.undo()) {
            std::cout << "Nichts zum Zurücknehmen.\n";
            return;
        }
        if (bot && game.position().sideToMove() == botColor && game.ply() > 0) game.undo();
    }

    void newGame() {
        game.reset();
        analysis.reset();
        if (stockfish) stockfish->newGame();
    }
};

}  // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    App app;
    std::cout << "ChessEngine (Konsole)\n";
    app.loadProfiles();
    if (auto sf = env::findStockfish(app.exeDir)) app.launchStockfish(*sf);
    app.showEngine();
    std::cout << "\n";
    printHelp();

    std::string input;
    while (true) {
        app.botTurn();
        printBoard(app.game.position());
        const GameOutcome out = app.game.outcome();
        if (out.over())
            std::cout << "Partie beendet: " << resultToString(out.result) << " (" << out.reason
                      << ")  – \"analyse\" bewertet alle Züge\n";
        else if (app.game.position().inCheck(app.game.position().sideToMove()))
            std::cout << "Schach!\n";

        std::cout << (app.game.position().sideToMove() == Color::White ? "Weiß" : "Schwarz") << " > ";
        if (!std::getline(std::cin, input)) break;
        input = trim(input);
        if (input.empty()) continue;

        const auto args = words(input);
        const std::string cmd = lower(args[0]);
        if (cmd == "ende") break;
        if (cmd == "hilfe") {
            printHelp();
        } else if (cmd == "zuege") {
            Position tmp = app.game.position();
            for (const Move& m : app.game.legalMoves()) std::cout << toSan(tmp, m) << ' ';
            std::cout << '\n';
        } else if (cmd == "zurueck") {
            app.undo();
        } else if (cmd == "bots") {
            app.listBots();
        } else if (cmd == "bot") {
            app.cmdBot(args);
        } else if (cmd == "analyse") {
            app.cmdAnalyse(args);
        } else if (cmd == "engine") {
            app.cmdEngine(args, input);
        } else if (cmd == "pgn") {
            app.printPgn();
        } else if (cmd == "fen") {
            std::cout << toFen(app.game.position()) << '\n';
        } else if (cmd == "neu") {
            app.newGame();
        } else if (!app.game.play(input)) {
            std::cout << "Ungültiger Zug: " << input << " (\"zuege\" zeigt alle Möglichkeiten)\n";
        }
    }
    return 0;
}
