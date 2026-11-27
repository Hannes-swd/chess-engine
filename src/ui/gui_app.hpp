#pragma once

#include "analysis/analysis.hpp"
#include "core/clock.hpp"
#include "core/game.hpp"
#include "core/pgn.hpp"
#include "engine/bot.hpp"
#include "engine/builtin_engine.hpp"
#include "engine/rating.hpp"
#include "engine/training.hpp"
#include "engine/uci_engine.hpp"
#include "ui/board_view.hpp"
#include "ui/image_cache.hpp"
#include "plugins/lua_plugin.hpp"
#include "ui/settings.hpp"
#include "ui/sound.hpp"
#include "ui/theme.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chess::ui {

// Bereiche der linken Navigation
enum class Mode { Home, Play, Analysis, Training, Bots, Games, Stats, Plugins, Settings };

// Die komplette Oberflaeche:
//   oben    Top-Leiste (Logo, Status, Elo, Bilanz, Engine, Design)
//   links   Navigation (Spielen, Analyse, Bots, Partien, Einstellungen), einklappbar
//   Mitte   Brett mit Spielerleisten und Uhren
//   rechts  Panel zum gewaehlten Bereich (z. B. neue Partie / Zuege)
// Bot-Zuege und Analysen laufen in Hintergrund-Threads, damit nichts ruckelt.
class GuiApp {
public:
    // sandbox: Testmodus (Screenshots) – nichts auf die Platte schreiben, schon ab dem Start
    explicit GuiApp(Theme& theme, bool sandbox = false);
    ~GuiApp();
    GuiApp(const GuiApp&) = delete;
    GuiApp& operator=(const GuiApp&) = delete;

    void frame();

    // Fuer Screenshots/Tests: Zuege nachspielen (SAN, durch Leerzeichen getrennt) und optional analysieren.
    void demo(const std::string& moves, bool analyse);
    // "e2e4" = Pfeil, "d5" = Kreis; optional ein Feld auswaehlen und einen Tipp anfordern.
    void demoMarks(const std::string& marks, const std::string& select, bool hint);
    // 0 Spielen/Zuege, 1 Bots, 2 Analyse, 3 Partien, 4 Einstellungen, 5 Spielen/Neue Partie,
    // 6 Training/Aufgaben, 7 Statistik, 8 Plugins, 9 Training/Eroeffnungen, 10 Startseite
    void requestTab(int tab) {
        static const Mode modes[] = {Mode::Play,     Mode::Bots,  Mode::Analysis, Mode::Games,    Mode::Settings,
                                     Mode::Play,     Mode::Training, Mode::Stats, Mode::Plugins, Mode::Training,
                                     Mode::Home};
        if (tab < 0 || tab > 10) return;
        trainTab_ = tab == 9 ? 1 : 0;
        mode_ = modes[tab];
        playTab_ = tab == 5 ? 0 : 1;
    }
    void requestView(int ply) { setView(ply); }
    void requestBotGame(int profile, int timePreset = -1) {
        selectedProfile_ = profile;
        colorChoice_ = 0;
        if (timePreset >= 0) tcPreset_ = timePreset;
        startNewGame();
    }
    void requestEditorOpen() { mode_ = Mode::Bots; }
    void requestOpen(const std::string& path) { openSavedGame(std::filesystem::path(path)); }
    void requestSidebar(bool expanded) { settings_.sidebarExpanded = expanded; }
    void requestPlugins(const std::vector<std::string>& ids) {  // fuer Screenshots: Plugins aktivieren
        settings_.plugins = ids;
        startNewGame();
    }
    void requestDrill(int opening) {  // fuer Screenshots: Aufgabe (-1) oder Eroeffnung starten
        mode_ = Mode::Training;
        if (opening < 0) {
            trainTab_ = 0;
            startPuzzle();
        } else {
            trainTab_ = 1;
            startOpeningDrill(static_cast<size_t>(opening));
        }
    }
    void requestBoardTheme(int i) { settings_.boardTheme = i, applyTheme(); }
    void requestPieceSet(int i) { settings_.pieceSet = i, applyTheme(); }
    void requestLanguage(int i) { settings_.language = i, applyTheme(); }
    void requestPreview(int steps) { requestedPreview_ = steps; }  // nach der Analyse: bessere Variante zeigen
    // Testmodus (Screenshots): nichts auf die Platte schreiben – keine Partien, keine Elo, keine Einstellungen
    void setSandbox(bool on) { sandbox_ = on; }
    // Fuer Bilder (Plugin-Icons, Vorschaubilder): der SDL-Renderer des Fensters
    void setRenderer(SDL_Renderer* r) { images_.setRenderer(r); }
    // Fuer Screenshots: Design/Hilfen setzen, ohne die gespeicherten Einstellungen zu aendern
    void overrideTheme(bool dark) {
        settings_.darkTheme = dark;
        applyTheme();
    }
    void overrideAssists(int preset) {  // 0 Lernen, 1 Standard, 2 Ohne Hilfen
        const bool dark = settings_.darkTheme, sidebar = settings_.sidebarExpanded;
        settings_ = preset == 0 ? AssistSettings::learning() : preset == 2 ? AssistSettings::none()
                                                                           : AssistSettings::standard();
        settings_.darkTheme = dark;
        settings_.sidebarExpanded = sidebar;
    }
    bool busy() const { return botThinking() || analysisRunning() || hintRunning(); }

private:
    // --- Spielablauf --------------------------------------------------------
    void startNewGame();
    void loadFen(const std::string& fen);
    void playMove(const Move& m);
    void undo();
    void setView(int ply);
    void pollJobs();
    void maybeStartBot();
    void startAnalysis();
    void startHint();
    std::optional<Move> currentHint() const;
    bool commitMove(const Move& m);  // Zug ausfuehren inkl. Uhr (Inkrement, Verlauf fuer Zuruecknehmen)
    void tickClock();
    void resign();
    TimeControl selectedTimeControl() const;
    void reloadProfiles();
    Engine& engineFor(EngineChoice choice);
    // Bot "denkt": Engine rechnet noch, oder der Zug wartet auf die menschliche Bedenkzeit
    bool botThinking() const { return botJob_.valid() || pendingBot_.has_value(); }
    bool analysisRunning() const { return analysisJob_.valid(); }
    bool hintRunning() const { return hintJob_.valid(); }
    bool engineBusy() const { return botJob_.valid() || analysisRunning() || hintRunning() || harvestRunning(); }
    bool harvestRunning() const { return harvestJob_.valid(); }

    // --- Zeichnen -------------------------------------------------------------
    void handleShortcuts();
    void drawBoardArea();
    void drawPlayerBar(Color side, ImVec2 min, ImVec2 max);
    void drawClock(Color side, ImVec2 min, ImVec2 max);
    void botEditor();
    void drawEvalBar(ImVec2 min, ImVec2 max);
    void drawResultCard();
    // Rahmen (layout.cpp)
    void drawTopBar(float height);
    void drawNav(float width);
    void drawRightPanel();
    void panelHeader(const char* glyph, const char* title);
    void panelPlay();
    void playSetup();
    void playMoves();
    void panelBots();
    bool iconButton(const char* id, const char* glyph, const char* tooltip, bool active = false, float width = 0,
                    bool enabled = true);
    bool navItem(const char* glyph, const char* label, bool active, float width);
    // Bausteine fuer ein ruhiges, eigenes Erscheinungsbild (layout.cpp)
    void sectionTitle(const char* text);
    bool toggleRow(const char* label, bool* value, const char* help = nullptr, float indent = 0);
    bool timeChip(const char* id, const char* big, const char* small, bool selected, ImVec2 size);
    void drawBotAvatar(ImDrawList* dl, ImVec2 center, float radius, const BotProfile* profile) const;
    bool botRow(const char* id, const BotProfile* profile, bool selected, bool showDescription);
    std::string figurine(const std::string& san) const;
    bool iconTab(const char* glyph, const char* label, bool active, float width);
    // Analyse-Bericht (analysis_panel.cpp)
    void drawEvalGraph(float height);
    void drawPreviewBanner(ImVec2 min, ImVec2 max);
    void startPreview(const Position& base, std::vector<Move> moves, std::string title);
    void setPreviewIndex(int index);
    // Wertung
    bool isRated() const;
    std::string unratedReason() const;
    void rateFinishedGame();
    // Panels mit bestehendem Inhalt
    void tabAnalysis();
    void tabExport();
    // Gespeicherte Partien (games_panel.cpp)
    void tabGames();
    void scanSavedGames();
    void openSavedGame(const std::filesystem::path& path);
    bool openPgn(const std::string& text, const std::filesystem::path& path);  // zum Nachspielen, nie gewertet
    void tabSettings();
    void saveSettings();
    void applyTheme();
    bool card(const char* id, const std::string& title, const std::string& chip, const std::string& desc,
              bool selected);
    bool primaryButton(const char* label, ImVec2 size = ImVec2(0, 0));

    // --- Plugins (plugins_panel.cpp) -----------------------------------------------
    void reloadPlugins();
    void applyPluginUi();  // ui.*-Aufrufe der Plugins ausfuehren (Meldung, Klang, Markierung)
    Variant currentVariant();  // Standardschach + aktive Plugins (fuer jede neue Partie neu gebaut)
    void panelPlugins();
    std::vector<plugins::PluginInfo> plugins_;
    std::vector<std::string> pluginErrors_;
    std::vector<std::shared_ptr<plugins::LuaPlugin>> gamePlugins_;  // Plugins der laufenden Partie (fuer das Log)
    bool pluginGame_ = false;  // laufende Partie nutzt Plugins -> ungewertet
    bool pluginPlayerStarts_ = false;  // ein aktives Plugin will, dass der Mensch den ersten Zug macht
    std::string pluginSel_;    // in der Plugin-Ansicht ausgewaehlt (Detailseite in der Mitte)
    struct PluginSummary {
        std::optional<Position> start;   // Startstellung des Plugins allein
        std::vector<std::string> facts;  // was es aendert, aus der gebauten Variante abgelesen
        std::string error;
    };
    std::map<std::string, PluginSummary> pluginSummaries_;
    const PluginSummary& pluginSummary(const plugins::PluginInfo& info);
    void drawPluginDetail();
    void drawPluginIcon(ImDrawList* dl, const plugins::PluginInfo& p, ImVec2 p0, float size);
    void drawMiniBoard(ImDrawList* dl, const Position& pos, ImVec2 p0, float size);
    ImageCache images_;

    // --- Spielgefuehl ----------------------------------------------------------------
    void playSounds(const Position& before, const Move& m);
    void playSound(Sound::Kind kind);
    void offerDraw();
    void saveResume();  // laufende Partie beim Beenden merken
    void loadResume();  // ... und beim naechsten Start anbieten
    void resumeGame();
    Sound sound_;
    bool soundReady_ = false;
    bool soundTried_ = false;
    std::optional<Score> lastBotScore_;  // Bewertung des letzten Bot-Zuges aus Sicht des Bots
    int endSoundGen_ = -1;
    int lastTickSecond_ = -1;
    std::optional<Move> premove_;
    int drawOfferPly_ = -100;
    std::string resumeText_;  // gespeicherte Partie (JSON), leer = nichts fortzusetzen
    std::string resumeInfo_;

    // --- Training (training_panel.cpp) --------------------------------------------------
    struct Drill {
        Position start{PieceRegistry::standard()};
        Position current{PieceRegistry::standard()};
        std::vector<Move> line;  // erwartete Zuege (Spieler und Gegner abwechselnd)
        Color user = Color::White;
        int index = 0;
        int mistakes = 0;
        bool done = false;
        bool revealed = false;
        std::string title, subtitle;
        int puzzle = -1;      // Index in training_.puzzles
        int opening = -1;     // Index in openings()
        double replyAt = 0;   // Gegnerzug automatisch ab diesem Zeitpunkt
        std::optional<Move> flash;
        bool flashGood = true;
        double flashUntil = 0;
        std::string feedback;
    };
    void panelTraining();
    void loadTraining();
    void saveTraining();
    void collectPuzzles();  // nach einer Analyse: eigene Fehler als Aufgaben
    void startPuzzle(int forced = -1);
    void startOpeningDrill(size_t opening);
    void drillMove(const Move& m);
    void tickDrill();
    void drawDrillBoard();
    bool drillActive() const { return mode_ == Mode::Training && drill_.has_value(); }
    TrainingData training_;
    std::optional<Drill> drill_;
    int trainTab_ = 0;
    char openingFilter_[64] = {};
    int collectedGen_ = -1;
    // Aufgaben aus gespeicherten Partien nachtraeglich sammeln (Hintergrund-Analyse)
    void startHarvest();
    void finishHarvest();  // in pollJobs: fertige Auswertung uebernehmen
    std::vector<std::filesystem::path> harvestCandidates();
    std::future<std::vector<Puzzle>> harvestJob_;
    std::vector<std::string> harvestFiles_;
    std::shared_ptr<std::atomic<int>> harvestDone_ = std::make_shared<std::atomic<int>>(0);
    int harvestTotal_ = 0;

    // --- Statistik (stats_panel.cpp) ------------------------------------------------------
    void panelStats();
    // --- Startseite (home_panel.cpp) -----------------------------------------------------------
    void drawHome();
    std::filesystem::path homeLastPath_;
    std::optional<Position> homeLastPos_;
    void drawEloCurve(float height);

    // --- Weiteres -----------------------------------------------------------------------
    void createOwnBot();
    void retryStockfish();

    // --- Hilfen ---------------------------------------------------------------
    const Position& viewedPosition() const;
    int viewPly() const { return view_ < 0 ? game_.ply() : view_; }
    bool isLive() const { return view_ < 0; }
    std::string playerName(Color side) const;   // Anzeige (Sprache)
    std::string pgnName(Color side) const;      // fuer Dateien (Deutsch, "Du")
    std::string displayPlayer(const std::string& stored) const;  // gespeicherter Name -> Anzeige
    std::string displaySource(const std::string& source) const;  // "Du – Clubspieler, 26.11.2026" -> Anzeige
    std::string resultTitle(const GameOutcome& out) const;
    std::string moveLabel(int ply) const;
    std::optional<Score> whiteEvalAt(int ply) const;
    const MoveReview* reviewFor(int ply) const;
    std::string pgnText() const;
    void savePgn(bool automatic);
    void setStatus(std::string msg);

    Theme& theme_;
    std::filesystem::path exeDir_;

    Mode mode_ = Mode::Play;
    int playTab_ = 0;  // im Bereich Spielen: 0 Neue Partie, 1 Zuege

    // Variante ansehen: bessere Fortsetzung oder Drohung Zug fuer Zug auf dem Brett zeigen
    struct Preview {
        Position base;
        Position current;
        std::vector<Move> moves;
        int index = 0;
        std::string title;
    };
    std::optional<Preview> preview_;
    int autoAnalysedGen_ = -1;
    int requestedPreview_ = -1;
    bool sandbox_ = false;

    // Elo-Wertung
    PlayerProfile player_;
    int ratedGen_ = -1;  // fuer welche Partie die Wertung schon gebucht ist
    int botElo_ = 0;     // geschaetzte Staerke des aktuellen Bots
    int lastRatedDelta_ = 0;
    bool undoUsed_ = false, hintUsed_ = false, fenGame_ = false;

    Game game_;
    int generation_ = 0;  // steigt bei jeder neuen Partie; alte Hintergrund-Ergebnisse werden verworfen
    int view_ = -1;       // -1 = aktuelle Stellung, sonst Halbzug-Index zum Nachspielen
    double lastMoveTime_ = 0;

    BuiltinEngine builtin_;
    std::unique_ptr<UciEngine> stockfish_;
    std::string engineInfo_;

    std::vector<BotProfile> profiles_;
    int selectedProfile_ = -1;  // -1 = zu zweit am Brett
    int colorChoice_ = 0;       // 0 Weiss, 1 Schwarz, 2 Zufall
    std::shared_ptr<Bot> bot_;
    Color botColor_ = Color::Black;
    std::string lastBotInfo_;
    std::string lastBotSan_;

    std::future<Bot::Choice> botJob_;
    int botJobGen_ = -1, botJobPly_ = -1;
    std::optional<Bot::Choice> pendingBot_;  // fertig gerechnet, wartet auf die Bedenkzeit
    int botDelayMs_ = 0;
    std::chrono::steady_clock::time_point botStarted_;
    int activeProfile_ = -1;  // Profil des laufenden Bots (fuer Live-Aenderungen)
    bool botProfileDirty_ = false;
    bool openEditor_ = false;

    // Uhr
    int tcPreset_ = 0;  // Index in kTimePresets, letzter = eigene
    float customMinutes_ = 10.0f;
    int customIncrement_ = 0;
    Clock clock_;
    std::vector<Clock::Snapshot> clockHistory_;  // Stand vor jedem Halbzug
    std::chrono::steady_clock::time_point lastTick_ = std::chrono::steady_clock::now();

    std::future<GameAnalysis> analysisJob_;
    std::shared_ptr<std::atomic<int>> progressDone_ = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> progressTotal_ = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<bool>> analysisStop_ = std::make_shared<std::atomic<bool>>(false);
    int analysisJobGen_ = -1, analysisJobPly_ = -1;
    std::optional<GameAnalysis> analysis_;
    int analysisPly_ = -1;
    int analysisDepth_ = 14;

    // Tipp: Engine schlaegt einen Zug fuer die aktuelle Stellung vor
    std::future<SearchResult> hintJob_;
    int hintJobGen_ = -1, hintJobPly_ = -1;
    std::optional<Move> hint_;
    int hintGen_ = -1, hintPly_ = -1;

    BoardView board_;
    AssistSettings settings_;
    bool flipped_ = false;
    bool zen_ = false;
    bool resultDismissed_ = false;
    int lastScrolledPly_ = -1;

    // Gespeicherte Partien: Liste im Bereich Partien; eine geoeffnete Partie behaelt ihre Namen und Tags
    struct SavedGame {
        std::filesystem::path path;
        std::string white, black, result, date, opening;
        int moves = 0;
        std::optional<double> accuracyYou;  // nur mit Bewertungen und wenn "Du" mitgespielt hat
        std::array<int, kQualityCount> countsYou{};
    };
    char gamesFilter_[64] = {};
    int gamesResultFilter_ = 0;  // 0 alle, 1 Siege, 2 Remis, 3 Niederlagen
    std::vector<SavedGame> savedGames_;
    double gamesScannedAt_ = -1000;
    PgnTags loadedTags_;

    std::filesystem::path savePath_;
    int savedGen_ = -1;
    std::string status_;
    double statusUntil_ = 0;
    char fenInput_[256] = {};
    int requestedTab_ = -1;
};

}  // namespace chess::ui
