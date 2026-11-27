#include "ui/gui_app.hpp"
#include "ui/i18n.hpp"

#include "app/environment.hpp"
#include "core/fen.hpp"
#include "core/notation.hpp"
#include "core/pgn.hpp"
#include "ui/gui_internal.hpp"
#include "ui/icons.hpp"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

namespace chess::ui {

namespace fs = std::filesystem;

namespace {

int materialOf(const Position& pos, Color c) {
    int sum = 0;
    for (Square s = 0; s < pos.squareCount(); ++s) {
        const Piece& p = pos.at(s);
        if (!p.empty() && p.color == c && !pos.registry().get(p.type).royal) sum += pos.registry().get(p.type).value;
    }
    return sum;
}

using detail::kTimePresetCount;
using detail::kTimePresets;
using detail::TimePreset;

int msSince(std::chrono::steady_clock::time_point t) {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count());
}

}  // namespace

// ===========================================================================
// Aufbau
// ===========================================================================

GuiApp::GuiApp(Theme& theme, bool sandbox) : theme_(theme), exeDir_(env::executableDir()) {
    sandbox_ = sandbox;
    plugins::setUiThread(std::this_thread::get_id());
    theme_.pieceImage = [this](const std::string& path) -> ImTextureID {
        const Image* img = images_.get(path, 256);
        return img ? img->id() : ImTextureID{};
    };
    settings_.darkTheme = theme_.dark;
    settings_.load(env::settingsFile());
    applyTheme();
    reloadProfiles();
    reloadPlugins();
    loadTraining();
    {
        std::ifstream in(env::profileFile(), std::ios::binary);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            playerProfileFromJson(ss.str(), player_);
        }
    }

    if (auto sf = env::findStockfish(exeDir_)) {
        std::string err;
        stockfish_ = UciEngine::launch(*sf, err);
        if (!stockfish_) setStatus(tr("Stockfish konnte nicht starten: ") + err);
    }
    engineInfo_ = stockfish_ ? stockfish_->name()
                             : tr("Eingebaute Engine (Stockfish nicht gefunden – siehe README)");
    analysisDepth_ = stockfish_ ? 14 : 8;

    // Vorauswahl: ein mittelstarker Gegner, die Partie startet aber zu zweit
    for (size_t i = 0; i < profiles_.size(); ++i)
        if (profiles_[i].name == "Clubspieler") selectedProfile_ = static_cast<int>(i);
    bot_.reset();
    const int keep = selectedProfile_;
    selectedProfile_ = -1;
    startNewGame();
    selectedProfile_ = keep;
    loadResume();  // nach startNewGame, das eine alte Fortsetzung sonst verwerfen wuerde
    mode_ = Mode::Home;
}

GuiApp::~GuiApp() {
    theme_.pieceImage = nullptr;  // Theme lebt laenger als die Bilder
    saveResume();
    analysisStop_->store(true);
    if (botJob_.valid()) botJob_.wait();
    if (analysisJob_.valid()) analysisJob_.wait();
    if (hintJob_.valid()) hintJob_.wait();
    if (harvestJob_.valid()) harvestJob_.wait();
}

// ===========================================================================
// Spielablauf
// ===========================================================================

Engine& GuiApp::engineFor(EngineChoice choice) {
    if (choice == EngineChoice::Builtin) return builtin_;
    if (stockfish_ && usesStandardRules(game_)) return *stockfish_;
    return builtin_;
}

void GuiApp::startNewGame() {
    if (analysisRunning()) analysisStop_->store(true);
    game_ = Game(currentVariant());
    premove_.reset();
    drawOfferPly_ = -100;
    lastTickSecond_ = -1;
    if (!resumeText_.empty()) {  // eine neue Partie ersetzt die gemerkte
        resumeText_.clear();
        if (!sandbox_) {
            std::error_code ec;
            fs::remove(env::resumeFile(), ec);
        }
    }
    ++generation_;
    view_ = -1;
    analysis_.reset();
    analysisPly_ = -1;
    savePath_.clear();
    resultDismissed_ = false;
    lastBotInfo_.clear();
    lastBotSan_.clear();
    board_.cancel();
    board_.clearAnnotations();
    hint_.reset();
    pendingBot_.reset();
    preview_.reset();
    loadedTags_.clear();
    lastMoveTime_ = ImGui::GetTime();
    clock_ = Clock(selectedTimeControl());
    clockHistory_.clear();
    lastTick_ = std::chrono::steady_clock::now();
    activeProfile_ = selectedProfile_;

    if (selectedProfile_ >= 0 && selectedProfile_ < static_cast<int>(profiles_.size())) {
        bot_ = std::make_shared<Bot>(profiles_[static_cast<size_t>(selectedProfile_)]);
        Color human = colorChoice_ == 1 ? Color::Black : Color::White;
        if (colorChoice_ == 2) human = std::random_device{}() % 2 ? Color::Black : Color::White;
        botColor_ = opposite(human);
        flipped_ = human == Color::Black;
        botElo_ = estimatedBotElo(bot_->profile(), stockfish_ != nullptr);
    } else {
        bot_.reset();
        flipped_ = false;
        botElo_ = 0;
    }
    // Aufgaben-Plugins (z. B. Matt in 1): du spielst immer die Seite, die am Zug ist
    if (bot_ && pluginPlayerStarts_) {
        botColor_ = opposite(game_.position().sideToMove());
        flipped_ = botColor_ == Color::White;
    }
    undoUsed_ = hintUsed_ = fenGame_ = false;
    lastRatedDelta_ = 0;
    if (stockfish_ && !engineBusy()) stockfish_->newGame();
    applyPluginUi();  // Meldungen aus on_setup
}

// ===========================================================================
// Elo-Wertung
// ===========================================================================

bool GuiApp::isRated() const { return unratedReason().empty(); }

std::string GuiApp::unratedReason() const {
    if (!bot_) return tr("Zu zweit am Brett wird nicht gewertet");
    if (fenGame_) return tr("Eigene Startstellung");
    if (pluginGame_) return tr("Mit Plugins gespielt");
    if (undoUsed_) return tr("Zug zurückgenommen");
    if (hintUsed_) return tr("Tipp benutzt");
    if (!usesStandardRules(game_)) return tr("Variante mit eigenen Regeln");
    return {};
}

void GuiApp::rateFinishedGame() {
    const GameOutcome out = game_.outcome();
    if (!out.over() || ratedGen_ == generation_) return;
    ratedGen_ = generation_;
    if (!isRated() || game_.ply() < 2) return;  // abgebrochene Partien zaehlen nicht

    const Color human = opposite(botColor_);
    double score = 0.5;
    if (out.result == GameResult::WhiteWins) score = human == Color::White ? 1.0 : 0.0;
    if (out.result == GameResult::BlackWins) score = human == Color::Black ? 1.0 : 0.0;
    lastRatedDelta_ = applyResult(player_, botElo_, score, env::timestamp(), bot_->profile().name);

    if (!sandbox_) {
        std::ofstream file(env::profileFile(), std::ios::binary);
        file << playerProfileToJson(player_);
    }
    setStatus(tr("Elo ") + std::to_string(player_.rating) + " (" + (lastRatedDelta_ >= 0 ? "+" : "") +
              std::to_string(lastRatedDelta_) + ")");
}

void GuiApp::loadFen(const std::string& fen) {
    try {
        Variant v = standardVariant();
        v.startFen = fen;
        Game g(v);
        startNewGame();  // Gegner-Einstellungen uebernehmen
        game_ = std::move(g);
        ++generation_;
        pluginGame_ = false;
        gamePlugins_.clear();
        flipped_ = bot_ ? botColor_ == Color::White : false;
        fenGame_ = true;
        setStatus(tr("Stellung geladen"));
    } catch (const std::exception& e) {
        setStatus(std::string(tr("FEN ungültig: ")) + e.what());
    }
}

bool GuiApp::commitMove(const Move& m) {
    const Color mover = game_.position().sideToMove();
    const Clock::Snapshot before = clock_.snapshot();
    const Position posBefore = game_.position();
    if (!game_.play(m)) return false;
    const Move played = game_.moves().back();
    if (settings_.animations) board_.animate(played);
    playSounds(posBefore, played);
    preview_.reset();
    clockHistory_.push_back(before);
    clock_.addIncrement(mover);
    board_.cancel();
    board_.clearAnnotations();
    lastMoveTime_ = ImGui::GetTime();
    applyPluginUi();
    return true;
}

void GuiApp::playMove(const Move& m) {
    if (!commitMove(m)) return;
    view_ = -1;
    if (mode_ == Mode::Play) playTab_ = 1;  // sobald gezogen wird: Zugliste zeigen
}

void GuiApp::tickClock() {
    const int elapsed = msSince(lastTick_);
    lastTick_ = std::chrono::steady_clock::now();
    if (!clock_.control().enabled() || game_.outcome().over() || game_.ply() == 0) return;  // Uhr laeuft ab dem 1. Zug

    const Color side = game_.position().sideToMove();
    clock_.consume(side, elapsed);
    if (!clock_.flagged(side)) return;

    pendingBot_.reset();
    const Color other = opposite(side);
    if (hasMatingMaterial(game_.position(), other))
        game_.setResult({other == Color::White ? GameResult::WhiteWins : GameResult::BlackWins, tr("Zeit abgelaufen")});
    else
        game_.setResult({GameResult::Draw, tr("Zeit abgelaufen, Gegner kann nicht mehr mattsetzen")});
}

void GuiApp::resign() {
    if (game_.outcome().over()) return;
    const Color loser = bot_ ? opposite(botColor_) : game_.position().sideToMove();
    pendingBot_.reset();
    game_.setResult({loser == Color::White ? GameResult::BlackWins : GameResult::WhiteWins,
                     std::string(loser == Color::White ? tr("Weiß") : tr("Schwarz")) + tr(" gibt auf")});
    view_ = -1;
}

TimeControl GuiApp::selectedTimeControl() const {
    const TimePreset& p = kTimePresets[std::clamp(tcPreset_, 0, kTimePresetCount - 1)];
    if (p.minutes < 0) return TimeControl::minutes(customMinutes_, customIncrement_);
    return TimeControl::minutes(p.minutes, p.increment);
}

void GuiApp::reloadProfiles() {
    std::vector<std::string> errors;
    profiles_ = loadBotProfiles(exeDir_ / "bots", errors);
    // Eigene Profile aus Dokumente\ChessEngine\Bots ersetzen gleichnamige oder kommen dazu
    for (BotProfile& own : loadBotProfiles(env::userBotsDir(), errors)) {
        auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const BotProfile& p) { return p.id == own.id; });
        if (it != profiles_.end())
            *it = std::move(own);
        else
            profiles_.push_back(std::move(own));
    }
    if (!errors.empty()) setStatus(tr("Bot-Profil fehlerhaft: ") + errors.front());
    if (selectedProfile_ >= static_cast<int>(profiles_.size())) selectedProfile_ = -1;
}

void GuiApp::undo() {
    if (botThinking()) {
        setStatus(tr("Der Bot denkt gerade – gleich nochmal versuchen"));
        return;
    }
    if (!game_.undo()) return;
    undoUsed_ = true;
    if (bot_ && game_.position().sideToMove() == botColor_ && game_.ply() > 0) game_.undo();
    // Uhr auf den Stand vor den zurueckgenommenen Zuegen
    const size_t ply = static_cast<size_t>(game_.ply());
    if (ply < clockHistory_.size()) {
        clock_.restore(clockHistory_[ply]);
        clockHistory_.resize(ply);
    }
    lastTick_ = std::chrono::steady_clock::now();
    if (analysis_ && game_.ply() < analysisPly_) {
        analysis_.reset();
        analysisPly_ = -1;
    }
    view_ = -1;
    resultDismissed_ = false;
    savedGen_ = -1;
    board_.cancel();
    lastMoveTime_ = ImGui::GetTime();
}

void GuiApp::demo(const std::string& moves, bool analyse) {
    std::string token;
    for (size_t i = 0; i <= moves.size(); ++i) {
        if (i == moves.size() || moves[i] == ' ') {
            if (!token.empty()) {
                Position tmp = game_.position();
                const auto m = parseMove(tmp, game_.legalMoves(), token);
                if (!m || !commitMove(*m)) setStatus(tr("Demo-Zug ungültig: ") + token);
            }
            token.clear();
        } else {
            token += moves[i];
        }
    }
    if (analyse) startAnalysis();
}

void GuiApp::demoMarks(const std::string& marks, const std::string& select, bool hint) {
    const Position& pos = game_.position();
    std::string token;
    int color = 0;
    for (size_t i = 0; i <= marks.size(); ++i) {
        if (i < marks.size() && marks[i] != ' ') {
            token += marks[i];
            continue;
        }
        if (token.size() == 2) {
            if (auto s = parseSquare(pos, token)) board_.addAnnotation(Annotation{*s, NoSquare, color});
        } else if (token.size() == 4) {
            auto a = parseSquare(pos, token.substr(0, 2)), b = parseSquare(pos, token.substr(2, 2));
            if (a && b) board_.addAnnotation(Annotation{*a, *b, color});
        }
        if (!token.empty()) color = (color + 1) % 4;  // Farben durchwechseln, damit man alle sieht
        token.clear();
    }
    if (auto s = parseSquare(pos, select)) board_.select(*s);
    if (hint) startHint();
}

void GuiApp::setView(int ply) {
    const int before = viewPly();
    view_ = (ply >= game_.ply()) ? -1 : std::max(0, ply);
    preview_.reset();
    // Einen Zug vor: die Figur gleiten lassen wie beim Spielen
    if (settings_.animations && viewPly() == before + 1 && before < game_.ply())
        board_.animate(game_.moves()[static_cast<size_t>(before)]);
    board_.cancel();
    if (viewPly() != before) board_.clearAnnotations();
}

void GuiApp::pollJobs() {
    using namespace std::chrono_literals;
    finishHarvest();
    if (botJob_.valid() && botJob_.wait_for(0ms) == std::future_status::ready) {
        Bot::Choice c = botJob_.get();
        if (botJobGen_ == generation_ && botJobPly_ == game_.ply() && c.move.valid()) pendingBot_ = std::move(c);
    }
    // Zug erst ausfuehren, wenn die (menschliche) Bedenkzeit um ist
    if (pendingBot_ && msSince(botStarted_) >= botDelayMs_) {
        const Bot::Choice c = *pendingBot_;
        pendingBot_.reset();
        if (!game_.outcome().over()) {
            Position tmp = game_.position();
            const std::string san = toSan(tmp, c.move);
            if (commitMove(c.move)) {
                lastBotSan_ = san;
                lastBotScore_ = c.score;
                lastBotInfo_ = san + ": " + tr(c.reason);
                if (c.score.mate || c.score.value != 0) lastBotInfo_ += ", " + c.score.toString();
                if (botDelayMs_ > 0) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), ", %.1f s", msSince(botStarted_) / 1000.0);
                    lastBotInfo_ += buf;
                }
                // Vorgemerkten Zug sofort ausfuehren, falls er jetzt legal ist
                if (premove_) {
                    const Move want = *premove_;
                    premove_.reset();
                    std::optional<Move> match;
                    for (const Move& lm : game_.legalMoves())
                        if (lm.from == want.from && lm.to == want.to &&
                            (!match || (lm.promotion != NoPieceType &&
                                        game_.position().registry().get(lm.promotion).id == "queen")))
                            match = lm;
                    if (match && !game_.outcome().over())
                        playMove(*match);
                    else
                        setStatus(tr("Vorzug nicht mehr möglich"));
                }
            }
        }
    }
    if (hintJob_.valid() && hintJob_.wait_for(0ms) == std::future_status::ready) {
        const SearchResult r = hintJob_.get();
        if (hintJobGen_ == generation_ && hintJobPly_ == game_.ply() && r.bestMove.valid()) {
            hint_ = r.bestMove;
            hintGen_ = hintJobGen_;
            hintPly_ = hintJobPly_;
            Position tmp = game_.position();
            std::string text = tr("Tipp: ") + toSan(tmp, r.bestMove);
            if (!r.lines.empty()) text += "  (" + r.lines.front().score.toString() + ")";
            setStatus(text);
        } else if (!r.ok) {
            setStatus(tr("Tipp fehlgeschlagen: ") + r.error);
        }
    }
    if (analysisJob_.valid() && analysisJob_.wait_for(0ms) == std::future_status::ready) {
        GameAnalysis a = analysisJob_.get();
        if (analysisJobGen_ == generation_) {
            if (a.ok) {
                analysis_ = std::move(a);
                analysisPly_ = analysisJobPly_;
                if (!savePath_.empty()) savePgn(true);  // gespeicherte Datei mit Bewertungen aktualisieren
                collectPuzzles();
                setStatus(tr("Analyse fertig"));
            } else {
                setStatus(a.error);
            }
        }
    }
}

void GuiApp::maybeStartBot() {
    if (!bot_ || engineBusy() || pendingBot_) return;
    // Aenderungen aus "Bot anpassen" uebernehmen – hier rechnet garantiert kein Thread mit dem Profil
    if (botProfileDirty_ && activeProfile_ >= 0 && activeProfile_ < static_cast<int>(profiles_.size()))
        bot_->setProfile(profiles_[static_cast<size_t>(activeProfile_)]);
    botProfileDirty_ = false;
    if (game_.outcome().over() || game_.position().sideToMove() != botColor_) return;
    if (ImGui::GetTime() - lastMoveTime_ < 0.15) return;  // Brett kurz zur Ruhe kommen lassen

    BotContext ctx;
    if (clock_.control().enabled()) {
        ctx.remainingMs = clock_.remaining(botColor_);
        ctx.incrementMs = clock_.control().incrementMs;
    }
    botDelayMs_ = bot_->thinkDelayMs(game_, ctx);  // vor dem Start: der Bot-Zufall laeuft nicht parallel
    botStarted_ = std::chrono::steady_clock::now();

    Engine* engine = &engineFor(bot_->profile().engine);
    botJobGen_ = generation_;
    botJobPly_ = game_.ply();
    botJob_ = std::async(std::launch::async,
                         [g = game_, b = bot_, engine, ctx]() { return b->choose(g, *engine, ctx); });
}

void GuiApp::startAnalysis() {
    if (engineBusy() || game_.ply() == 0) return;
    Engine* engine = &engineFor(EngineChoice::Auto);
    SearchLimits lim;
    lim.depth = analysisDepth_;
    if (engine == &builtin_) {
        lim.depth = std::min(analysisDepth_, 8);
        lim.movetimeMs = 300;
    }
    analysisStop_ = std::make_shared<std::atomic<bool>>(false);
    progressDone_->store(0);
    progressTotal_->store(game_.ply() + 1);
    lim.stop = analysisStop_.get();

    analysisJobGen_ = generation_;
    analysisJobPly_ = game_.ply();
    analysisJob_ = std::async(std::launch::async, [g = game_, engine, lim, stop = analysisStop_,
                                                   done = progressDone_, total = progressTotal_]() {
        (void)stop;  // haelt das Flag am Leben, solange die Analyse laeuft
        return analyseGame(g, *engine, lim, {}, [done, total](int d, int t) {
            done->store(d);
            total->store(t);
        });
    });
}

void GuiApp::startHint() {
    if (engineBusy() || game_.outcome().over()) return;
    if (bot_ && game_.position().sideToMove() == botColor_) return;  // Tipp nur fuer den eigenen Zug
    if (currentHint()) return;  // schon da
    Engine* engine = &engineFor(EngineChoice::Auto);
    SearchLimits lim;
    lim.multiPv = 1;
    if (engine == &builtin_) {
        lim.depth = 8;
        lim.movetimeMs = 500;
    } else {
        lim.depth = 14;
        lim.movetimeMs = 800;
    }
    if (!game_.variant().hooks.filterMoves.empty()) lim.rootMoves = game_.legalMoves();
    if (stockfish_ && engine == stockfish_.get()) stockfish_->configureStrength(20, 0);  // Tipp in voller Staerke
    hintJobGen_ = generation_;
    hintJobPly_ = game_.ply();
    hintUsed_ = true;
    if (!isLive()) setView(game_.ply());  // Tipp gilt fuer die aktuelle Stellung
    hintJob_ = std::async(std::launch::async, [pos = game_.position(), g = game_, engine, lim]() {
        SearchLimits l = lim;
        l.game = &g;  // Plugin-Regeln beachten
        return engine->search(pos, l);
    });
}

std::optional<Move> GuiApp::currentHint() const {
    if (hint_ && hintGen_ == generation_ && hintPly_ == game_.ply()) return hint_;
    return std::nullopt;
}

// ===========================================================================
// Hilfen
// ===========================================================================

const Position& GuiApp::viewedPosition() const { return view_ < 0 ? game_.position() : game_.positionAt(view_); }

// Name fuer Dateien (PGN, Statistik): immer Deutsch, "Du" fuer dich – daran erkennen Statistik und Aufgaben deine Partien
std::string GuiApp::pgnName(Color side) const {
    if (bot_) return side == botColor_ ? bot_->profile().name : "Du";
    for (const auto& [k, v] : loadedTags_)  // geoeffnete Partie: Namen aus der Datei
        if (k == (side == Color::White ? "White" : "Black") && !v.empty() && v != "?") return v;
    return side == Color::White ? "Weiß" : "Schwarz";
}

// Name fuer die Anzeige, in der gewaehlten Sprache
std::string GuiApp::playerName(Color side) const { return displayPlayer(pgnName(side)); }

std::string GuiApp::displaySource(const std::string& source) const {
    const std::string sep = " – ";
    const size_t a = source.find(sep);
    const size_t b = source.rfind(", ");
    if (a == std::string::npos || b == std::string::npos || b < a) return source;
    return displayPlayer(source.substr(0, a)) + sep + displayPlayer(source.substr(a + sep.size(), b - a - sep.size())) +
           source.substr(b);
}

std::string GuiApp::displayPlayer(const std::string& stored) const {
    if (stored == "Du" || stored == "Weiß" || stored == "Schwarz") return tr(stored);
    for (const BotProfile& p : profiles_)
        if (p.name == stored) return p.displayName(english());
    return stored;
}

std::string GuiApp::resultTitle(const GameOutcome& out) const {
    if (out.result == GameResult::Draw) return tr("Remis");
    const Color winner = out.result == GameResult::WhiteWins ? Color::White : Color::Black;
    if (bot_) return winner == botColor_ ? tr("Verloren") : tr("Gewonnen!");
    return winner == Color::White ? tr("Weiß gewinnt") : tr("Schwarz gewinnt");
}

std::string GuiApp::moveLabel(int ply) const {
    const Position& start = game_.startPosition();
    const int offset = start.sideToMove() == Color::Black ? 1 : 0;
    const int number = start.fullmoveNumber() + (ply + offset) / 2;
    const bool white = (ply + offset) % 2 == 0;
    return std::to_string(number) + (white ? ". " : "... ") + game_.sanMoves()[static_cast<size_t>(ply)];
}

const MoveReview* GuiApp::reviewFor(int ply) const {
    if (!analysis_ || ply < 0 || ply >= static_cast<int>(analysis_->moves.size())) return nullptr;
    return &analysis_->moves[static_cast<size_t>(ply)];
}

std::optional<Score> GuiApp::whiteEvalAt(int ply) const {
    if (!analysis_ || ply < 0 || ply >= static_cast<int>(analysis_->whiteEvals.size())) return std::nullopt;
    return analysis_->whiteEvals[static_cast<size_t>(ply)];
}

std::string GuiApp::pgnText() const {
    PgnTags tags;
    if (!loadedTags_.empty()) {
        // Geoeffnete Partie: Datum, Namen, Zeit usw. aus der Datei behalten (Ergebnis/FEN setzt toPgn)
        for (const auto& [k, v] : loadedTags_)
            if (k != "Result" && k != "Annotator" && k != "SetUp" && k != "FEN") tags.emplace_back(k, v);
    } else {
        std::string date = env::timestamp().substr(0, 10);
        std::replace(date.begin(), date.end(), '-', '.');
        tags.emplace_back("Date", date);
        tags.emplace_back("White", pgnName(Color::White));
        tags.emplace_back("Black", pgnName(Color::Black));
        if (clock_.control().enabled()) tags.emplace_back("TimeControl", clock_.control().pgnTag());
    }
    std::vector<std::string> comments;
    if (analysis_ && analysisPly_ <= game_.ply()) {
        comments = pgnComments(*analysis_);
        tags.emplace_back("Annotator", analysis_->engineName);
    }
    return toPgn(game_, tags, comments.empty() ? nullptr : &comments);
}

void GuiApp::savePgn(bool automatic) {
    if (sandbox_) {
        savedGen_ = generation_;  // Testmodus: so tun, als waere gespeichert
        return;
    }
    if (savePath_.empty()) savePath_ = env::gamesDir() / (env::timestamp() + ".pgn");
    std::ofstream out(savePath_, std::ios::binary);
    out << pgnText();
    if (!out) {
        setStatus(tr("Speichern fehlgeschlagen: ") + savePath_.string());
        return;
    }
    savedGen_ = generation_;
    if (!automatic) setStatus(tr("Gespeichert: ") + savePath_.filename().string());
}

void GuiApp::setStatus(std::string msg) {
    status_ = std::move(msg);
    statusUntil_ = ImGui::GetTime() + 5.0;
}

// ===========================================================================
// Frame
// ===========================================================================

void GuiApp::handleShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    if (preview_) {  // Variante: Pfeiltasten blaettern in der Variante, Esc kehrt zur Partie zurueck
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) setPreviewIndex(preview_->index - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) setPreviewIndex(preview_->index + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) preview_.reset();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) setView(viewPly() - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) setView(viewPly() + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) setView(0);
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) setView(game_.ply());
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) board_.cancel();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false) && settings_.undo) undo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) startNewGame();
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) flipped_ = !flipped_;
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) zen_ = !zen_;
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_H, false) && settings_.hintButton) startHint();
}

void GuiApp::frame() {
    tickClock();
    pollJobs();
    maybeStartBot();
    handleShortcuts();

    // Partie automatisch speichern und werten, sobald sie vorbei ist
    if (game_.outcome().over() && savedGen_ != generation_) savePgn(true);
    rateFinishedGame();
    tickDrill();
    // Klang am Partieende und Ticken in den letzten 10 Sekunden der eigenen Uhr
    if (game_.outcome().over() && endSoundGen_ != generation_ && game_.ply() > 0) {
        endSoundGen_ = generation_;
        playSound(Sound::Kind::End);
    }
    if (clock_.control().enabled() && !game_.outcome().over() && game_.ply() > 0) {
        const Color side = game_.position().sideToMove();
        const int ms = clock_.remaining(side);
        if ((!bot_ || side != botColor_) && ms < 10000 && ms / 1000 != lastTickSecond_) {
            lastTickSecond_ = ms / 1000;
            playSound(Sound::Kind::Tick);
        }
    }    // Screenshot-Hilfe: nach der Analyse die bessere Variante des angezeigten Zuges oeffnen
    if (requestedPreview_ >= 0 && analysis_)
        if (const MoveReview* r = reviewFor(viewPly() - 1); r && !r->bestLine.empty()) {
            const int view = viewPly();
            startPreview(game_.positionAt(view - 1), r->bestLine, tr("Besser war ") + figurine(r->bestSan));
            setPreviewIndex(requestedPreview_);
            view_ = view;  // startPreview veraendert die Ansicht nicht, nur zur Sicherheit
            requestedPreview_ = -1;
        }
    // Nach dem Ende automatisch analysieren, sobald die Engine frei ist
    if (settings_.autoAnalyse && game_.outcome().over() && autoAnalysedGen_ != generation_ && game_.ply() >= 2 &&
        !engineBusy() && !analysis_) {
        autoAnalysedGen_ = generation_;
        startAnalysis();
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##main", nullptr, flags);
    ImGui::PopStyleVar();

    const float s = theme_.scale;
    const ImVec2 full = ImGui::GetContentRegionAvail();
    const float topH = zen_ ? 0.0f : 56.0f * s;
    const float navW = zen_ ? 0.0f : (settings_.sidebarExpanded ? 204.0f : 66.0f) * s;
    const float margin = zen_ ? 0.0f : 14.0f * s;
    const float panelW = zen_ ? 0.0f : std::min(396.0f * s, full.x * 0.34f);
    const ImGuiWindowFlags still = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (!zen_) {
        // Oben: Top-Leiste ueber die ganze Breite
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme_.p().surface);
        ImGui::BeginChild("##top", ImVec2(full.x, topH), ImGuiChildFlags_None, still);
        drawTopBar(topH);
        ImGui::EndChild();
        // Links: Navigation
        ImGui::SetCursorPos(ImVec2(0, topH));
        ImGui::BeginChild("##nav", ImVec2(navW, full.y - topH), ImGuiChildFlags_None, still);
        drawNav(navW);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // Startseite: volle Breite, ohne Brett und rechtes Panel
    if (mode_ == Mode::Home && !zen_) {
        ImGui::SetCursorPos(ImVec2(navW, topH));
        ImGui::BeginChild("##home", ImVec2(full.x - navW, full.y - topH), ImGuiChildFlags_None);
        drawHome();
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    // Mitte: Brett
    ImGui::SetCursorPos(ImVec2(navW, topH));
    ImGui::BeginChild("##boardarea", ImVec2(full.x - navW - panelW - margin, full.y - topH),
                      ImGuiChildFlags_AlwaysUseWindowPadding, still);
    drawBoardArea();
    ImGui::EndChild();

    // Rechts: Panel zum gewaehlten Bereich
    if (!zen_) {
        ImGui::SetCursorPos(ImVec2(full.x - panelW - margin, topH + margin));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme_.p().surface);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f * s);
        ImGui::BeginChild("##panel", ImVec2(panelW, full.y - topH - 2 * margin),
                          ImGuiChildFlags_AlwaysUseWindowPadding, still);
        drawRightPanel();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

// ===========================================================================
// Brettbereich
// ===========================================================================

void GuiApp::drawBoardArea() {
    const float s = theme_.scale;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float barH = 34.0f * s;
    const float gap = 8.0f * s;
    const bool hasEval = settings_.evalBar && whiteEvalAt(viewPly()).has_value();
    const float evalW = hasEval ? 12.0f * s + gap : 0.0f;

    if (drillActive()) {
        drawDrillBoard();
        return;
    }
    if (mode_ == Mode::Plugins && !zen_) {  // Plugin-Ansicht: Detailseite statt Brett
        drawPluginDetail();
        return;
    }
    const GameOutcome out = game_.outcome();
    const bool humanTurn = !bot_ || game_.position().sideToMove() != botColor_;
    const bool interactive = !preview_ && isLive() && !out.over() && humanTurn && !botThinking();
    // Waehrend der Bot am Zug ist, darf man einen Zug vormerken (eigene Figuren, als waere man dran)
    const bool premoveMode = settings_.premoves && bot_ && !preview_ && isLive() && !out.over() && !humanTurn;
    Position premovePos = game_.position();
    if (premoveMode) {
        premovePos.setSideToMove(opposite(botColor_));
        premovePos.setEnPassant(NoSquare);
    }
    const Position& pos = preview_ ? preview_->current : premoveMode ? premovePos : viewedPosition();
    const std::vector<Move> legal = interactive    ? game_.legalMoves()
                                    : premoveMode ? premovePos.legalMoves()
                                                  : std::vector<Move>{};

    BoardOverlay ov;
    ov.showCoordinates = settings_.coordinates;
    ov.showHints = settings_.legalMoves;
    ov.showDanger = settings_.dangerTargets;
    ov.showCheck = settings_.checkGlow;
    ov.showTargetFrame = settings_.targetFrame;
    ov.allowAnnotations = settings_.annotations;
    if (settings_.hangingPieces && isLive() && !out.over())
        ov.hangingFor = bot_ ? opposite(botColor_) : game_.position().sideToMove();
    const int ply = viewPly();
    if (preview_) {
        // Variante: letzter gezeigter Zug markiert, naechster Zug als Pfeil
        const Preview& p = *preview_;
        if (p.index > 0) ov.lastMove = p.moves[static_cast<size_t>(p.index - 1)];
        if (p.index < static_cast<int>(p.moves.size()))
            ov.arrows.push_back({p.moves[static_cast<size_t>(p.index)], Theme::u32(theme_.p().arrow)});
    } else {
        if (ply > 0 && settings_.lastMove) ov.lastMove = game_.moves()[static_cast<size_t>(ply - 1)];
        if (settings_.bestArrow)
            if (const MoveReview* r = reviewFor(ply); r && r->best.valid())
                ov.arrows.push_back({r->best, Theme::u32(theme_.p().arrow)});
        // Tipp in Messing (wie die Analyse) – Gruen bleibt den eigenen Pfeilen vorbehalten
        if (const auto h = currentHint(); h && isLive()) ov.arrows.push_back({*h, Theme::u32(theme_.p().arrow)});
        // Bewertungs-Abzeichen auf dem Zielfeld des letzten Zuges
        if (settings_.moveMarks && ply > 0)
            if (const MoveReview* r = reviewFor(ply - 1)) {
                ov.badgeSquare = r->played.to;
                ov.badgeQuality = r->quality;
            }
    }

    const ImVec2 boardOrigin(origin.x + evalW, origin.y + barH + gap);
    const ImVec2 boardAvail(avail.x - evalW, avail.y - 2 * (barH + gap));
    ov.premove = premove_;
    if (auto mv = board_.draw(pos, legal, interactive || premoveMode, flipped_, ov, theme_, boardOrigin, boardAvail)) {
        if (premoveMode) {
            premove_ = *mv;
            setStatus(tr("Zug vorgemerkt – Rechtsklick hebt ihn auf"));
        } else {
            playMove(*mv);
        }
    }
    if (premove_ && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        ImGui::IsMouseHoveringRect(board_.boardMin(), board_.boardMax()))
        premove_.reset();

    const ImVec2 bmin = board_.boardMin(), bmax = board_.boardMax();
    const Color top = flipped_ ? Color::White : Color::Black;
    if (preview_)
        drawPreviewBanner(ImVec2(bmin.x, bmin.y - gap - barH), ImVec2(bmax.x, bmin.y - gap));
    else
        drawPlayerBar(top, ImVec2(bmin.x, bmin.y - gap - barH), ImVec2(bmax.x, bmin.y - gap));
    drawPlayerBar(opposite(top), ImVec2(bmin.x, bmax.y + gap), ImVec2(bmax.x, bmax.y + gap + barH));
    if (hasEval) drawEvalBar(ImVec2(bmin.x - evalW, bmin.y), ImVec2(bmin.x - gap, bmax.y));

    if (out.over() && isLive() && !resultDismissed_) drawResultCard();

    if (zen_) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + avail.x - ImGui::GetFrameHeight() * 1.3f, origin.y));
        if (iconButton("##zenoff", icon::fullscreenExit, tr("Zen-Modus beenden (Z)"))) zen_ = false;
    }
}

void GuiApp::drawPlayerBar(Color side, ImVec2 min, ImVec2 max) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float midY = (min.y + max.y) * 0.5f;
    const bool toMove = isLive() && !game_.outcome().over() && game_.position().sideToMove() == side;

    // Abzeichen: Bots mit ihrem Staerke-Avatar, Menschen mit der Farbe ihrer Steine.
    // Wer am Zug ist, bekommt einen Messingring.
    const bool isBot = bot_ && side == botColor_;
    const float r = 13 * s;
    const ImVec2 av(min.x + r + 1 * s, midY);
    if (isBot) {
        drawBotAvatar(dl, av, r, &bot_->profile());
    } else {
        dl->AddCircleFilled(av, r, Theme::u32(side == Color::White ? c.whiteFill : c.blackFill));
        dl->AddCircle(av, r, Theme::u32(c.textMuted, 0.6f), 0, 1.0f * s);
    }
    if (toMove) dl->AddCircle(av, r + 3.5f * s, Theme::u32(c.accent), 0, 2.0f * s);

    const Position& pos = viewedPosition();
    const int diff = materialOf(pos, side) - materialOf(pos, opposite(side));
    ImFont* sf = theme_.strong ? theme_.strong : ImGui::GetFont();
    ImFont* hf = theme_.heading ? theme_.heading : sf;
    const float fs = 17.0f * s;
    const std::string label = playerName(side);
    const ImVec2 nsz = sf->CalcTextSizeA(fs, FLT_MAX, 0.0f, label.c_str());
    float x = av.x + r + 12 * s;
    dl->AddText(sf, fs, ImVec2(x, midY - nsz.y * 0.5f), Theme::u32(c.text), label.c_str());
    x += nsz.x + 8 * s;

    auto smallNum = [&](const std::string& t, ImU32 col) {
        const ImVec2 sz = hf->CalcTextSizeA(16 * s, FLT_MAX, 0.0f, t.c_str());
        dl->AddText(hf, 16 * s, ImVec2(x, midY - sz.y * 0.5f), col, t.c_str());
        x += sz.x + 10 * s;
    };
    if (isBot) smallNum(std::to_string(botElo_), Theme::u32(c.textMuted));
    if (settings_.material && diff >= 100) smallNum("+" + std::to_string((diff + 50) / 100), Theme::u32(c.qBest));

    // Bot denkt: drei pulsierende Punkte statt Text
    if (isBot && botThinking()) {
        const double t = ImGui::GetTime();
        for (int i = 0; i < 3; ++i) {
            const float a = 0.35f + 0.65f * static_cast<float>(0.5 + 0.5 * std::sin(t * 6.0 - i * 0.9));
            dl->AddCircleFilled(ImVec2(x + i * 10 * s + 3 * s, midY), 3 * s, Theme::u32(c.accent, a));
        }
    }
    if (clock_.control().enabled()) drawClock(side, min, max);
}

void GuiApp::drawClock(Color side, ImVec2 min, ImVec2 max) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int ms = clock_.remaining(side);
    const bool running = !game_.outcome().over() && game_.ply() > 0 && game_.position().sideToMove() == side;
    const bool low = ms < 10000;

    const std::string text = Clock::format(ms);
    ImFont* font = theme_.heading ? theme_.heading : ImGui::GetFont();
    const float fs = 24.0f * s;
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text.c_str());
    const float w = std::max(112.0f * s, ts.x + 28 * s);
    const ImVec2 bmin(max.x - w, min.y), bmax(max.x, max.y);

    // Wie eine Schachuhr: die laufende Seite hell wie ein Ziffernblatt, die andere ruht dunkel.
    // In Zeitnot wird die laufende Uhr rot.
    ImVec4 bg = c.surfaceRaised, fg = c.textMuted;
    if (running) {
        bg = low ? c.qBlunder : c.text;
        fg = low ? ImVec4(1, 1, 1, 1) : c.bg;
    } else if (ms == 0) {
        fg = c.qBlunder;
    }
    dl->AddRectFilled(bmin, bmax, Theme::u32(bg), 6 * s);
    dl->AddText(font, fs, ImVec2(bmax.x - 14 * s - ts.x, (bmin.y + bmax.y - ts.y) * 0.5f), Theme::u32(fg),
                text.c_str());
}

void GuiApp::drawEvalBar(ImVec2 min, ImVec2 max) {
    const Palette& c = theme_.p();
    const auto score = whiteEvalAt(viewPly());
    if (!score) return;
    double f = 0.5;
    if (score->mate)
        f = score->winning ? 1.0 : 0.0;
    else
        f = 0.5 + 0.5 * std::tanh(score->value / 400.0);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = (max.x - min.x) * 0.5f;
    const float h = max.y - min.y;
    dl->AddRectFilled(min, max, Theme::u32(c.blackFill), r);
    const float whiteH = static_cast<float>(h * f);
    // Weiss sitzt auf der Seite, auf der die weissen Figuren stehen
    if (flipped_)
        dl->AddRectFilled(min, ImVec2(max.x, min.y + whiteH), Theme::u32(c.whiteFill), r);
    else
        dl->AddRectFilled(ImVec2(min.x, max.y - whiteH), max, Theme::u32(c.whiteFill), r);
    dl->AddRect(min, max, Theme::u32(c.border), r);

    if (ImGui::IsMouseHoveringRect(min, max)) ImGui::SetTooltip(tr("Bewertung: %s"), score->toString().c_str());
}

void GuiApp::drawResultCard() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const GameOutcome out = game_.outcome();
    const ImVec2 bmin = board_.boardMin(), bmax = board_.boardMax();
    const float w = std::min(400.0f * s, (bmax.x - bmin.x) * 0.88f);
    const float h = 214.0f * s;
    const ImVec2 min((bmin.x + bmax.x - w) * 0.5f, (bmin.y + bmax.y - h) * 0.5f);
    const ImVec2 max(min.x + w, min.y + h);
    const float pad = 22 * s;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(bmin, bmax, Theme::u32(c.bg, 0.45f));
    // Weicher Schatten, damit die Karte ueber dem Brett liegt statt darin zu kleben
    for (int i = 4; i >= 1; --i)
        dl->AddRectFilled(ImVec2(min.x - i * 2 * s, min.y - i * s), ImVec2(max.x + i * 2 * s, max.y + i * 3 * s),
                          IM_COL32(0, 0, 0, 18), 16 * s);
    dl->AddRectFilled(min, max, Theme::u32(c.surface), 14 * s);
    dl->AddRect(min, max, Theme::u32(c.border), 14 * s);

    // Ergebnis gross in Bahnschrift, daneben Titel und Grund
    ImFont* hf = theme_.heading ? theme_.heading : ImGui::GetFont();
    const std::string score = out.result == GameResult::WhiteWins   ? "1–0"
                              : out.result == GameResult::BlackWins ? "0–1"
                                                                    : "½–½";
    const ImVec2 ssz = hf->CalcTextSizeA(46 * s, FLT_MAX, 0.0f, score.c_str());
    dl->AddText(hf, 46 * s, ImVec2(min.x + pad, min.y + pad - 4 * s), Theme::u32(c.accent), score.c_str());
    const float tx = min.x + pad + ssz.x + 18 * s;
    dl->AddText(hf, 26 * s, ImVec2(tx, min.y + pad), Theme::u32(c.text), resultTitle(out).c_str());
    dl->AddText(ImGui::GetFont(), 16 * s, ImVec2(tx, min.y + pad + 30 * s), Theme::u32(c.textMuted), tr(out.reason).c_str());

    // Elo-Aenderung oder warum nicht gewertet wurde
    const float rowY = min.y + pad + ssz.y + 10 * s;
    if (ratedGen_ == generation_ && isRated() && game_.ply() >= 2) {
        const ImVec4 col = lastRatedDelta_ > 0 ? c.qBest : lastRatedDelta_ < 0 ? c.qBlunder : c.textMuted;
        const std::string elo = std::to_string(player_.rating);
        const std::string delta = (lastRatedDelta_ >= 0 ? "+" : "-") + std::to_string(std::abs(lastRatedDelta_));
        Theme::glyphCentered(dl, theme_.body, 22 * s, ImVec2(min.x + pad + 11 * s, rowY + 12 * s), Theme::u32(c.accent),
                             icon::rating);
        dl->AddText(hf, 22 * s, ImVec2(min.x + pad + 28 * s, rowY), Theme::u32(c.text), elo.c_str());
        const float ew = hf->CalcTextSizeA(22 * s, FLT_MAX, 0.0f, elo.c_str()).x;
        dl->AddText(hf, 20 * s, ImVec2(min.x + pad + 36 * s + ew, rowY + 1 * s), Theme::u32(col), delta.c_str());
    } else if (bot_) {
        dl->AddText(ImGui::GetFont(), 15 * s, ImVec2(min.x + pad, rowY + 3 * s), Theme::u32(c.textMuted),
                    (tr("Ungewertet: ") + unratedReason()).c_str());
    }

    // Aktionen
    ImGui::SetCursorScreenPos(ImVec2(min.x + pad, max.y - pad - 44 * s));
    ImGui::PushFont(theme_.strong, 0.0f);
    if (primaryButton((std::string(icon::analysis) + tr("  Analysieren")).c_str(), ImVec2(0, 44 * s))) {
        startAnalysis();
        mode_ = Mode::Analysis;
        resultDismissed_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(icon::refresh) + tr("  Neue Partie")).c_str(), ImVec2(0, 44 * s))) startNewGame();
    ImGui::PopFont();

    // Schliessen dezent oben rechts
    const float btn = 22.0f * s + 2 * ImGui::GetStyle().FramePadding.y;
    ImGui::SetCursorScreenPos(ImVec2(max.x - btn - 8 * s, min.y + 8 * s));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, c.textMuted);
    if (iconButton("##closecard", icon::close, tr("Schließen"))) resultDismissed_ = true;
    ImGui::PopStyleColor(2);
}

// ===========================================================================
// Seitenpanel
// ===========================================================================

bool GuiApp::primaryButton(const char* label, ImVec2 size) {
    const Palette& c = theme_.p();
    ImGui::PushStyleColor(ImGuiCol_Button, c.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.accent.x * 1.08f, c.accent.y * 1.08f, c.accent.z * 1.08f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(c.accent.x * 0.92f, c.accent.y * 0.92f, c.accent.z * 0.92f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, c.accentText);
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return clicked;
}

bool GuiApp::card(const char* id, const std::string& title, const std::string& chip, const std::string& desc,
                  bool selected) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const float pad = 12 * s;
    const float wrap = width - 2 * pad;
    const float lineH = ImGui::GetTextLineHeight();
    const ImVec2 descSize = desc.empty() ? ImVec2(0, 0) : ImGui::CalcTextSize(desc.c_str(), nullptr, false, wrap);
    const float h = pad * 2 + lineH + (desc.empty() ? 0 : 4 * s + descSize.y);

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + width, p0.y + h);
    const ImVec4 bg = selected ? ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.13f)
                               : (hovered ? c.surfaceRaised : ImVec4(c.surfaceRaised.x, c.surfaceRaised.y,
                                                                     c.surfaceRaised.z, 0.55f));
    dl->AddRectFilled(p0, p1, Theme::u32(bg), 9 * s);
    if (selected) dl->AddRect(p0, p1, Theme::u32(c.accent), 9 * s, 0, 1.5f * s);

    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    dl->AddText(font, fs, ImVec2(p0.x + pad, p0.y + pad), Theme::u32(c.text), title.c_str());
    if (!chip.empty()) {
        const float cw = ImGui::CalcTextSize(chip.c_str()).x;
        dl->AddText(font, fs, ImVec2(p1.x - pad - cw, p0.y + pad), Theme::u32(selected ? c.accent : c.textMuted),
                    chip.c_str());
    }
    if (!desc.empty())
        dl->AddText(font, fs, ImVec2(p0.x + pad, p0.y + pad + lineH + 4 * s), Theme::u32(c.textMuted), desc.c_str(),
                    nullptr, wrap);
    return clicked;
}

// ===========================================================================
// Einstellungen: jede Hilfe einzeln
// ===========================================================================

void GuiApp::saveSettings() {
    if (sandbox_) return;
    if (!settings_.save(env::settingsFile())) setStatus(tr("Einstellungen konnten nicht gespeichert werden"));
}

void GuiApp::applyTheme() {
    theme_.dark = settings_.darkTheme;
    theme_.setBoard(settings_.boardTheme);
    theme_.pieceSet = std::clamp(settings_.pieceSet, 0, 2);
    setLanguage(settings_.language == 1 ? Language::English : Language::German);
    plugins::setLanguage(settings_.language == 1);
    pluginSummaries_.clear();  // "Was es aendert" in der neuen Sprache
    theme_.apply();
}

void GuiApp::tabSettings() {
    const float s = theme_.scale;
    const ImVec4 accentSoft(theme_.p().accent.x, theme_.p().accent.y, theme_.p().accent.z, 0.30f);
    bool changed = false;
    auto option = [&](const char* label, bool* value, const char* help) {
        changed |= toggleRow(label, value, help);
    };

    ImGui::BeginChild("##settings", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::Dummy(ImVec2(0, 2 * s));
    sectionTitle(tr("Sprache"));
    {
        const float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        const char* langs[2] = {"Deutsch", "English"};
        for (int i = 0; i < 2; ++i) {
            if (i) ImGui::SameLine();
            const bool active = settings_.language == i;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, accentSoft);
            if (ImGui::Button(langs[i], ImVec2(w2, 0)) && !active) {
                settings_.language = i;
                applyTheme();
                changed = true;
            }
            if (active) ImGui::PopStyleColor();
        }
    }
    sectionTitle(tr("Schnellwahl"));
    struct Preset {
        const char* label;
        AssistSettings value;
        const char* help;
    };
    const Preset presets[] = {
        {tr("Lernen"), AssistSettings::learning(), tr("Alle Hilfen an, auch hängende Figuren")},
        {tr("Standard"), AssistSettings::standard(), tr("Die üblichen Hilfen")},
        {tr("Ohne Hilfen"), AssistSettings::none(), tr("Wie im Turnier: keine Punkte, Tipps, Zurücknehmen oder Bewertungen")},
    };
    const float w3 = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        const bool active = settings_.sameAssists(presets[i].value);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, accentSoft);
        if (ImGui::Button(presets[i].label, ImVec2(w3, 0))) {
            // Nur die Hilfen wechseln; Design, Klang, Plugins usw. bleiben
            const AssistSettings keep = settings_;
            settings_ = presets[i].value;
            settings_.darkTheme = keep.darkTheme;
            settings_.coordinates = keep.coordinates;
            settings_.sidebarExpanded = keep.sidebarExpanded;
            settings_.autoAnalyse = keep.autoAnalyse;
            settings_.animations = keep.animations;
            settings_.sounds = keep.sounds;
            settings_.premoves = keep.premoves;
            settings_.boardTheme = keep.boardTheme;
            settings_.pieceSet = keep.pieceSet;
            settings_.language = keep.language;
            settings_.plugins = keep.plugins;
            changed = true;
        }
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", presets[i].help);
    }

    sectionTitle(tr("Beim Ziehen"));
    option(tr("Mögliche Züge anzeigen (Punkte)"), &settings_.legalMoves,
           tr("Nach dem Anklicken einer Figur erscheinen Punkte auf allen Feldern, wohin sie ziehen kann."));
    ImGui::BeginDisabled(!settings_.legalMoves);
    changed |= toggleRow(tr("Gefährliche Zielfelder rot"), &settings_.dangerTargets,
                         tr("Punkte werden rot, wenn die Figur dort Material verliert: ungedeckt angegriffen oder von einer billigeren Figur schlagbar. Braucht „Mögliche Züge“."),
                         22 * theme_.scale);
    ImGui::EndDisabled();
    option(tr("Zielfeld unter der Maus umrahmen"), &settings_.targetFrame, nullptr);

    sectionTitle(tr("Während der Partie"));
    option(tr("Gefährdete eigene Figuren markieren"), &settings_.hangingPieces,
           tr("Roter Rahmen um eigene Figuren, die gerade Material verlieren: ungedeckt angegriffen oder von einer billigeren Figur bedroht (z. B. Dame von Springer)."));
    option(tr("Schach am König hervorheben"), &settings_.checkGlow, nullptr);
    option(tr("Letzten Zug hervorheben"), &settings_.lastMove, nullptr);
    option(tr("Materialvorteil anzeigen (+2)"), &settings_.material, nullptr);
    option(tr("Tipp-Funktion (H)"), &settings_.hintButton, tr("Die Engine zeigt auf Wunsch einen guten Zug."));
    option(tr("Zurücknehmen erlauben"), &settings_.undo, nullptr);
    option(tr("Bewertung des Bots anzeigen"), &settings_.botInfo,
           tr("Begründung und Stellungsbewertung nach jedem Bot-Zug – verrät, wer besser steht."));
    option(tr("Eigene Pfeile & Kreise (Rechtsklick)"), &settings_.annotations, nullptr);
    option(tr("Züge vormerken, während der Bot denkt"), &settings_.premoves,
           tr("Eigene Figur ziehen, solange der Bot am Zug ist: Der Zug wird blau vorgemerkt und sofort ausgeführt, wenn er danach noch erlaubt ist. Rechtsklick hebt ihn auf."));

    sectionTitle(tr("Analyse"));
    option(tr("Bewertungsbalken neben dem Brett"), &settings_.evalBar, nullptr);
    option(tr("Besten Zug als Pfeil zeigen"), &settings_.bestArrow, nullptr);
    option(tr("?!  ?  ??  in der Zugliste"), &settings_.moveMarks, nullptr);

    sectionTitle(tr("Brett"));
    {
        // Kleine Vorschau je Design: 2 x 2 Felder
        const auto& themes = Theme::boardThemes();
        const int perRow = 3;
        const float gapX = 6 * s;
        const float tw = (ImGui::GetContentRegionAvail().x - (perRow - 1) * gapX) / perRow;
        const float th = 58 * s;
        for (int i = 0; i < static_cast<int>(themes.size()); ++i) {
            if (i % perRow) ImGui::SameLine(0, gapX);
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##board", ImVec2(tw, th))) {
                settings_.boardTheme = i;
                applyTheme();
                changed = true;
            }
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float q = 16 * s;
            const ImVec2 b0(p0.x + 8 * s, p0.y + (th - 2 * q) * 0.5f);
            for (int k = 0; k < 4; ++k) {
                const ImVec2 a(b0.x + (k % 2) * q, b0.y + (k / 2) * q);
                const bool light = (k % 2) == (k / 2);
                dl->AddRectFilled(a, ImVec2(a.x + q, a.y + q), Theme::u32(light ? themes[static_cast<size_t>(i)].light
                                                                                : themes[static_cast<size_t>(i)].dark));
            }
            const bool sel = settings_.boardTheme == i;
            if (sel || hovered)
                dl->AddRect(p0, ImVec2(p0.x + tw, p0.y + th),
                            Theme::u32(sel ? theme_.p().accent : theme_.p().border), 8 * s, 0, sel ? 1.5f * s : 1.0f * s);
            dl->AddText(ImGui::GetFont(), 15 * s, ImVec2(b0.x + 2 * q + 8 * s, p0.y + th * 0.5f - 9 * s),
                        Theme::u32(sel ? theme_.p().text : theme_.p().textMuted), tr(themes[static_cast<size_t>(i)].name));
        }
    }

    sectionTitle(tr("Figuren"));
    {
        const char* names[3] = {tr("Klassisch"), tr("Diagramm"), tr("Buchstaben")};
        const float gapX = 6 * s;
        const float tw = (ImGui::GetContentRegionAvail().x - 2 * gapX) / 3;
        const float th = 70 * s;
        PieceType king, knight;
        king.id = "king", king.symbol = 'K', king.royal = true;
        knight.id = "knight", knight.symbol = 'N';
        const int keepSet = theme_.pieceSet;
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine(0, gapX);
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImGui::PushID(100 + i);
            if (ImGui::InvisibleButton("##pieces", ImVec2(tw, th))) {
                settings_.pieceSet = i;
                applyTheme();
                changed = true;
            }
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const bool sel = settings_.pieceSet == i;
            dl->AddRectFilled(p0, ImVec2(p0.x + tw, p0.y + th), Theme::u32(theme_.p().boardLight, 0.9f), 8 * s);
            if (sel || hovered)
                dl->AddRect(p0, ImVec2(p0.x + tw, p0.y + th), Theme::u32(sel ? theme_.p().accent : theme_.p().border), 8 * s,
                            0, sel ? 2.0f * s : 1.0f * s);
            theme_.pieceSet = i;
            const float cell = 34 * s;
            BoardView::drawPiece(dl, theme_, king, Color::White, ImVec2(p0.x + tw * 0.5f - cell * 0.5f, p0.y + 26 * s), cell);
            BoardView::drawPiece(dl, theme_, knight, Color::Black, ImVec2(p0.x + tw * 0.5f + cell * 0.5f, p0.y + 26 * s), cell);
            theme_.pieceSet = keepSet;
            const ImVec2 ns = ImGui::GetFont()->CalcTextSizeA(14 * s, FLT_MAX, 0.0f, names[i]);
            dl->AddText(ImGui::GetFont(), 14 * s, ImVec2(p0.x + (tw - ns.x) * 0.5f, p0.y + th - 20 * s),
                        Theme::u32(sel ? theme_.p().bg : theme_.p().bg, sel ? 1.0f : 0.75f), names[i]);
        }
    }

    sectionTitle(tr("Darstellung und Klang"));
    option(tr("Figuren gleiten lassen"), &settings_.animations, tr("Kurze Bewegung statt Springen, auch beim Nachspielen."));
    option(tr("Klänge"), &settings_.sounds, tr("Leise Klänge für Zug, Schlag, Schach, Zeitnot und Partieende."));
    option(tr("Partie nach dem Ende automatisch analysieren"), &settings_.autoAnalyse, nullptr);
    option(tr("Koordinaten am Brettrand"), &settings_.coordinates, nullptr);
    if (toggleRow(tr("Dunkles Design"), &settings_.darkTheme)) {
        applyTheme();
        changed = true;
    }

    sectionTitle(tr("Engine"));
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(engineInfo_.c_str());
    if (!stockfish_) {
        ImGui::TextDisabled("%s", tr("Stockfish ist kostenlos. Lade die Windows-Version herunter, entpacke sie und lege stockfish.exe in den Ordner engines neben dem Programm."));
        ImGui::PopTextWrapPos();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button((std::string(icon::openNew) + tr("  Download-Seite")).c_str()))
            SDL_OpenURL("https://stockfishchess.org/download/");
        ImGui::SameLine();
        if (ImGui::Button((std::string(icon::folder) + tr("  Ordner engines")).c_str())) {
            std::error_code ec;
            fs::create_directories(exeDir_ / "engines", ec);
            SDL_OpenURL(("file:///" + (exeDir_ / "engines").generic_string()).c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(icon::sync) + tr("  Erneut suchen")).c_str())) retryStockfish();
        ImGui::PopStyleColor();
    } else {
        ImGui::PopTextWrapPos();
    }

    ImGui::Dummy(ImVec2(0, 4 * s));
    ImGui::TextDisabled(tr("Wird automatisch gespeichert."));
    ImGui::EndChild();

    if (changed) {
        saveSettings();
        if (!settings_.hintButton) hint_.reset();
        board_.cancel();
    }
}

void GuiApp::botEditor() {
    const float s = theme_.scale;
    BotProfile& p = profiles_[static_cast<size_t>(selectedProfile_)];
    bool changed = false;
    auto tip = [](const char* text) {
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
    };

    ImGui::PushItemWidth(-FLT_MIN);
    sectionTitle(tr("Name und Beschreibung"));
    {
        char name[64] = {};
        std::snprintf(name, sizeof(name), "%s", p.name.c_str());
        if (ImGui::InputText("##botname", name, sizeof(name))) {
            p.name = name;
            changed = true;
        }
        char desc[600] = {};
        std::snprintf(desc, sizeof(desc), "%s", p.description.c_str());
        if (ImGui::InputTextMultiline("##botdesc", desc, sizeof(desc), ImVec2(-FLT_MIN, 70 * s))) {
            p.description = desc;
            changed = true;
        }
    }
    sectionTitle(tr("Stärke"));
    // Empfohlen: das menschliche Modell mit einer Ziel-Elo. Die alte Stockfish-Begrenzung
    // ist selbst auf der niedrigsten Stufe noch sehr stark.
    bool human = p.strength > 0;
    if (toggleRow(tr("Wie ein Mensch dieser Stärke spielen"), &human,
                  tr("Der Bot schaut nur so weit voraus wie ein Spieler dieser Elo, wählt öfter einen schwächeren Zug und patzt gelegentlich."))) {
        p.strength = human ? 1200 : 0;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Der Bot schaut nur so weit voraus wie ein Spieler dieser Elo, wählt öfter einen schwächeren Zug und patzt gelegentlich."));
    if (human) {
        changed |= ImGui::SliderInt("##strength", &p.strength, 400, 3000, tr("Ziel-Elo %d"));
        const StrengthModel m = strengthModel(p.strength);
        ImGui::TextDisabled(tr("Schaut %d Halbzüge voraus, Aussetzer bei etwa %d %% der Züge"), m.depth,
                            static_cast<int>(std::lround(m.blunder * 100)));
    } else {
        bool useElo = p.elo > 0;
        if (toggleRow(tr("Stockfish auf Elo begrenzen"), &useElo)) {
            p.elo = useElo ? 1500 : 0;
            changed = true;
        }
        if (useElo)
            changed |= ImGui::SliderInt("##elo", &p.elo, 1320, 3190, tr("Elo %d"));
        else
            changed |= ImGui::SliderInt("##skill", &p.skill, 0, 20, tr("Stufe %d / 20"));
        if (!stockfish_) ImGui::TextDisabled(tr("Stufe und Elo-Begrenzung wirken nur mit Stockfish."));
    }

    sectionTitle(tr("Spielstil"));
    float aggr = static_cast<float>(p.aggression);
    if (ImGui::SliderFloat("##aggr", &aggr, -1.0f, 1.0f, aggr < -0.05f ? tr("vorsichtig  %.2f")
                                                          : aggr > 0.05f  ? tr("aggressiv  %.2f")
                                                                          : tr("ausgeglichen  %.2f"))) {
        p.aggression = aggr;
        changed = true;
    }
    tip(tr("Aggressiv bevorzugt Schläge, Schachs und Vorstöße; vorsichtig meidet sie."));

    sectionTitle(tr("Zufall & Fehler"));
    float rnd = static_cast<float>(p.randomness);
    if (ImGui::SliderFloat("##rnd", &rnd, 0.0f, 200.0f, tr("Zufall  %.0f cp"))) {
        p.randomness = rnd;
        changed = true;
    }
    tip(tr("Rauschen auf die Bewertung: höher = unberechenbarer."));
    float blunder = static_cast<float>(p.blunderChance * 100.0);
    if (ImGui::SliderFloat("##blunder", &blunder, 0.0f, 50.0f, tr("Patzer-Wahrscheinlichkeit  %.0f %%"))) {
        p.blunderChance = blunder / 100.0;
        changed = true;
    }
    tip(tr("So oft spielt der Bot absichtlich einen zufälligen Zug."));
    changed |= ImGui::SliderInt("##cand", &p.candidates, 1, 10, tr("%d Kandidatenzüge"));
    tip(tr("Aus so vielen guten Zügen wählt der Bot nach Stil und Zufall."));
    changed |= ImGui::SliderInt("##window", &p.window, 0, 400, tr("Toleranz  %d cp"));
    tip(tr("So viel schlechter als der beste Zug darf ein Kandidat höchstens sein."));

    sectionTitle(tr("Tempo"));
    int mode = static_cast<int>(p.tempo.mode);
    // Eintraege durch \0 getrennt; als String gebaut, damit sie uebersetzt werden koennen
    const std::string tempoItems = std::string(tr("Sofort")) + '\0' + tr("Fest") + '\0' + tr("Wie ein Mensch") + '\0';
    if (ImGui::Combo("##tempo", &mode, tempoItems.c_str())) {
        p.tempo.mode = static_cast<TempoMode>(mode);
        changed = true;
    }
    tip(tr("Wie ein Mensch: zufällige Bedenkzeit, schneller bei klaren Zügen, länger in komplizierten Stellungen."));
    if (p.tempo.mode != TempoMode::Instant) {
        float lo = p.tempo.minMs / 1000.0f, hi = p.tempo.maxMs / 1000.0f;
        if (p.tempo.mode == TempoMode::Fixed) {
            if (ImGui::SliderFloat("##fixed", &lo, 0.1f, 30.0f, tr("%.1f s pro Zug"))) {
                p.tempo.minMs = static_cast<int>(lo * 1000);
                p.tempo.maxMs = std::max(p.tempo.maxMs, p.tempo.minMs);
                changed = true;
            }
        } else {
            if (ImGui::DragFloatRange2("##range", &lo, &hi, 0.05f, 0.1f, 60.0f, tr("min %.1f s"), tr("max %.1f s"))) {
                p.tempo.minMs = static_cast<int>(lo * 1000);
                p.tempo.maxMs = std::max(p.tempo.minMs, static_cast<int>(hi * 1000));
                changed = true;
            }
            float var = static_cast<float>(p.tempo.variance);
            if (ImGui::SliderFloat("##var", &var, 0.0f, 1.5f, tr("Schwankung  %.2f"))) {
                p.tempo.variance = var;
                changed = true;
            }
            tip(tr("Wie unterschiedlich lange der Bot nachdenkt."));
        }
    }
    float pressure = static_cast<float>(p.pressureBlunder * 100.0);
    if (ImGui::SliderFloat("##pressure", &pressure, 0.0f, 100.0f, tr("Nervosität in Zeitnot  %.0f %%"))) {
        p.pressureBlunder = pressure / 100.0;
        changed = true;
    }
    tip(tr("Unter 10 Sekunden auf der Uhr steigt die Fehlerwahrscheinlichkeit um bis zu diesen Wert."));
    ImGui::PopItemWidth();

    ImGui::Dummy(ImVec2(0, 2 * s));
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    if (ImGui::Button(tr("Speichern"), ImVec2(half, 0))) {
        const fs::path file = sandbox_ ? fs::path() : env::userBotsDir() / ((p.id.empty() ? std::string("eigener_bot") : p.id) + ".json");
        std::ofstream out;
        if (!sandbox_) out.open(file, std::ios::binary);
        out << botProfileToJson(p);
        setStatus(out ? tr("Bot gespeichert: ") + file.filename().string() : tr("Speichern fehlgeschlagen"));
    }
    tip(tr("Speichert nach Dokumente\\ChessEngine\\Bots – überschreibt die Vorlage beim nächsten Start."));
    ImGui::SameLine();
    if (ImGui::Button(tr("Zurücksetzen"), ImVec2(half, 0))) {
        reloadProfiles();
        changed = true;
    }

    if (changed && selectedProfile_ == activeProfile_ && bot_) botProfileDirty_ = true;
}

}  // namespace chess::ui
