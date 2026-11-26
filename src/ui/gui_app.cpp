#include "ui/gui_app.hpp"

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

GuiApp::GuiApp(Theme& theme) : theme_(theme), exeDir_(env::executableDir()) {
    settings_.darkTheme = theme_.dark;
    settings_.load(env::settingsFile());
    applyTheme();
    reloadProfiles();
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
        if (!stockfish_) setStatus("Stockfish konnte nicht starten: " + err);
    }
    engineInfo_ = stockfish_ ? stockfish_->name()
                             : "Eingebaute Engine (Stockfish nicht gefunden – siehe README)";
    analysisDepth_ = stockfish_ ? 14 : 8;

    // Vorauswahl: ein mittelstarker Gegner, die Partie startet aber zu zweit
    for (size_t i = 0; i < profiles_.size(); ++i)
        if (profiles_[i].name == "Clubspieler") selectedProfile_ = static_cast<int>(i);
    std::snprintf(fenInput_, sizeof(fenInput_), "%s", kStandardStartFen);
    bot_.reset();
    const int keep = selectedProfile_;
    selectedProfile_ = -1;
    startNewGame();
    selectedProfile_ = keep;
}

GuiApp::~GuiApp() {
    analysisStop_->store(true);
    if (botJob_.valid()) botJob_.wait();
    if (analysisJob_.valid()) analysisJob_.wait();
    if (hintJob_.valid()) hintJob_.wait();
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
    game_ = Game(standardVariant());
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
    undoUsed_ = hintUsed_ = fenGame_ = false;
    lastRatedDelta_ = 0;
    if (stockfish_ && !engineBusy()) stockfish_->newGame();
}

// ===========================================================================
// Elo-Wertung
// ===========================================================================

bool GuiApp::isRated() const { return unratedReason().empty(); }

std::string GuiApp::unratedReason() const {
    if (!bot_) return "Zu zweit am Brett wird nicht gewertet";
    if (fenGame_) return "Eigene Startstellung";
    if (undoUsed_) return "Zug zurückgenommen";
    if (hintUsed_) return "Tipp benutzt";
    if (!usesStandardRules(game_)) return "Variante mit eigenen Regeln";
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
    lastRatedDelta_ = applyResult(player_, botElo_, score);

    std::ofstream file(env::profileFile(), std::ios::binary);
    file << playerProfileToJson(player_);
    setStatus("Elo " + std::to_string(player_.rating) + " (" + (lastRatedDelta_ >= 0 ? "+" : "") +
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
        flipped_ = bot_ ? botColor_ == Color::White : false;
        fenGame_ = true;
        setStatus("Stellung geladen");
    } catch (const std::exception& e) {
        setStatus(std::string("FEN ungültig: ") + e.what());
    }
}

bool GuiApp::commitMove(const Move& m) {
    const Color mover = game_.position().sideToMove();
    const Clock::Snapshot before = clock_.snapshot();
    if (!game_.play(m)) return false;
    preview_.reset();
    clockHistory_.push_back(before);
    clock_.addIncrement(mover);
    board_.cancel();
    board_.clearAnnotations();
    lastMoveTime_ = ImGui::GetTime();
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
        game_.setResult({other == Color::White ? GameResult::WhiteWins : GameResult::BlackWins, "Zeit abgelaufen"});
    else
        game_.setResult({GameResult::Draw, "Zeit abgelaufen, Gegner kann nicht mehr mattsetzen"});
}

void GuiApp::resign() {
    if (game_.outcome().over()) return;
    const Color loser = bot_ ? opposite(botColor_) : game_.position().sideToMove();
    pendingBot_.reset();
    game_.setResult({loser == Color::White ? GameResult::BlackWins : GameResult::WhiteWins,
                     std::string(loser == Color::White ? "Weiß" : "Schwarz") + " gibt auf"});
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
    if (!errors.empty()) setStatus("Bot-Profil fehlerhaft: " + errors.front());
    if (selectedProfile_ >= static_cast<int>(profiles_.size())) selectedProfile_ = -1;
}

void GuiApp::undo() {
    if (botThinking()) {
        setStatus("Der Bot denkt gerade – gleich nochmal versuchen");
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
                if (!m || !commitMove(*m)) setStatus("Demo-Zug ungültig: " + token);
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
    board_.cancel();
    if (viewPly() != before) board_.clearAnnotations();
}

void GuiApp::pollJobs() {
    using namespace std::chrono_literals;
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
                lastBotInfo_ = san + ": " + c.reason;
                if (c.score.mate || c.score.value != 0) lastBotInfo_ += ", " + c.score.toString();
                if (botDelayMs_ > 0) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), ", %.1f s", msSince(botStarted_) / 1000.0);
                    lastBotInfo_ += buf;
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
            std::string text = "Tipp: " + toSan(tmp, r.bestMove);
            if (!r.lines.empty()) text += "  (" + r.lines.front().score.toString() + ")";
            setStatus(text);
        } else if (!r.ok) {
            setStatus("Tipp fehlgeschlagen: " + r.error);
        }
    }
    if (analysisJob_.valid() && analysisJob_.wait_for(0ms) == std::future_status::ready) {
        GameAnalysis a = analysisJob_.get();
        if (analysisJobGen_ == generation_) {
            if (a.ok) {
                analysis_ = std::move(a);
                analysisPly_ = analysisJobPly_;
                if (!savePath_.empty()) savePgn(true);  // gespeicherte Datei mit Bewertungen aktualisieren
                setStatus("Analyse fertig");
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
    hintJob_ = std::async(std::launch::async, [pos = game_.position(), engine, lim]() { return engine->search(pos, lim); });
}

std::optional<Move> GuiApp::currentHint() const {
    if (hint_ && hintGen_ == generation_ && hintPly_ == game_.ply()) return hint_;
    return std::nullopt;
}

// ===========================================================================
// Hilfen
// ===========================================================================

const Position& GuiApp::viewedPosition() const { return view_ < 0 ? game_.position() : game_.positionAt(view_); }

std::string GuiApp::playerName(Color side) const {
    if (bot_) return side == botColor_ ? bot_->profile().name : "Du";
    return side == Color::White ? "Weiß" : "Schwarz";
}

std::string GuiApp::resultTitle(const GameOutcome& out) const {
    if (out.result == GameResult::Draw) return "Remis";
    const Color winner = out.result == GameResult::WhiteWins ? Color::White : Color::Black;
    if (bot_) return winner == botColor_ ? "Verloren" : "Gewonnen!";
    return winner == Color::White ? "Weiß gewinnt" : "Schwarz gewinnt";
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
    std::string date = env::timestamp().substr(0, 10);
    std::replace(date.begin(), date.end(), '-', '.');
    tags.emplace_back("Date", date);
    tags.emplace_back("White", playerName(Color::White));
    tags.emplace_back("Black", playerName(Color::Black));
    if (clock_.control().enabled()) tags.emplace_back("TimeControl", clock_.control().pgnTag());
    std::vector<std::string> comments;
    if (analysis_ && analysisPly_ <= game_.ply()) {
        comments = pgnComments(*analysis_);
        tags.emplace_back("Annotator", analysis_->engineName);
    }
    return toPgn(game_, tags, comments.empty() ? nullptr : &comments);
}

void GuiApp::savePgn(bool automatic) {
    if (savePath_.empty()) savePath_ = env::gamesDir() / (env::timestamp() + ".pgn");
    std::ofstream out(savePath_, std::ios::binary);
    out << pgnText();
    if (!out) {
        setStatus("Speichern fehlgeschlagen: " + savePath_.string());
        return;
    }
    savedGen_ = generation_;
    if (!automatic) setStatus("Gespeichert: " + savePath_.filename().string());
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
    // Screenshot-Hilfe: nach der Analyse die bessere Variante des angezeigten Zuges oeffnen
    if (requestedPreview_ >= 0 && analysis_)
        if (const MoveReview* r = reviewFor(viewPly() - 1); r && !r->bestLine.empty()) {
            const int view = viewPly();
            startPreview(game_.positionAt(view - 1), r->bestLine, "Besser war " + r->bestSan);
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

    const Position& pos = preview_ ? preview_->current : viewedPosition();
    const GameOutcome out = game_.outcome();
    const bool humanTurn = !bot_ || game_.position().sideToMove() != botColor_;
    const bool interactive = !preview_ && isLive() && !out.over() && humanTurn && !botThinking();
    const std::vector<Move> legal = interactive ? game_.legalMoves() : std::vector<Move>{};

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
    if (auto mv = board_.draw(pos, legal, interactive, flipped_, ov, theme_, boardOrigin, boardAvail)) playMove(*mv);

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
        if (iconButton("##zenoff", icon::fullscreenExit, "Zen-Modus beenden (Z)")) zen_ = false;
    }
}

void GuiApp::drawPlayerBar(Color side, ImVec2 min, ImVec2 max) {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float midY = (min.y + max.y) * 0.5f;
    const bool toMove = isLive() && !game_.outcome().over() && game_.position().sideToMove() == side;

    // Farbscheibe des Spielers, Messingring wenn am Zug
    const ImVec2 disc(min.x + 10 * s, midY);
    dl->AddCircleFilled(disc, 7 * s, Theme::u32(side == Color::White ? c.whiteFill : c.blackFill));
    dl->AddCircle(disc, 7 * s, Theme::u32(c.border), 0, 1.0f * s);
    if (toMove) dl->AddCircle(disc, 10.5f * s, Theme::u32(c.accent), 0, 2.0f * s);

    std::string label = playerName(side);
    const Position& pos = viewedPosition();
    const int diff = materialOf(pos, side) - materialOf(pos, opposite(side));
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const ImVec2 textPos(min.x + 26 * s, midY - fs * 0.5f);
    dl->AddText(font, fs, textPos, Theme::u32(c.text), label.c_str());
    float x = textPos.x + ImGui::CalcTextSize(label.c_str()).x + 10 * s;

    if (settings_.material && diff >= 100) {
        const std::string adv = "+" + std::to_string((diff + 50) / 100);
        dl->AddText(font, fs, ImVec2(x, textPos.y), Theme::u32(c.textMuted), adv.c_str());
        x += ImGui::CalcTextSize(adv.c_str()).x + 10 * s;
    }
    if (bot_ && side == botColor_ && botThinking()) {
        const int dots = static_cast<int>(ImGui::GetTime() * 3.0) % 4;
        const std::string t = "denkt" + std::string(static_cast<size_t>(dots), '.');
        dl->AddText(font, fs, ImVec2(x, textPos.y), Theme::u32(c.accent), t.c_str());
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
    const float fs = 20.0f * s;
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text.c_str());
    const float w = std::max(92.0f * s, ts.x + 24 * s);
    const ImVec2 bmin(max.x - w, min.y + 2 * s), bmax(max.x, max.y - 2 * s);

    // Laufende Uhr: Messing-Hintergrund; knapp: rot. Sonst zurueckhaltend.
    ImVec4 bg = c.surfaceRaised, fg = c.textMuted;
    if (running) {
        bg = low ? ImVec4(c.qBlunder.x, c.qBlunder.y, c.qBlunder.z, 0.28f) : ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.22f);
        fg = low ? c.qBlunder : c.text;
    } else if (ms == 0) {
        fg = c.qBlunder;
    }
    dl->AddRectFilled(bmin, bmax, Theme::u32(bg), 7 * s);
    if (running) dl->AddRect(bmin, bmax, Theme::u32(low ? c.qBlunder : c.accent, 0.7f), 7 * s, 0, 1.2f * s);
    dl->AddText(font, fs, ImVec2(bmax.x - 12 * s - ts.x, (bmin.y + bmax.y - ts.y) * 0.5f), Theme::u32(fg),
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

    if (ImGui::IsMouseHoveringRect(min, max)) ImGui::SetTooltip("Bewertung: %s", score->toString().c_str());
}

void GuiApp::drawResultCard() {
    const Palette& c = theme_.p();
    const float s = theme_.scale;
    const GameOutcome out = game_.outcome();
    const ImVec2 bmin = board_.boardMin(), bmax = board_.boardMax();
    const float w = std::min(360.0f * s, (bmax.x - bmin.x) * 0.85f);
    const float h = 196.0f * s;
    const ImVec2 min((bmin.x + bmax.x - w) * 0.5f, (bmin.y + bmax.y - h) * 0.5f);
    const ImVec2 max(min.x + w, min.y + h);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(bmin, bmax, Theme::u32(c.bg, 0.35f));
    dl->AddRectFilled(min, max, Theme::u32(c.surface, 0.97f), 14 * s);
    dl->AddRect(min, max, Theme::u32(c.border), 14 * s);

    const float pad = 20 * s;
    ImGui::SetCursorScreenPos(ImVec2(min.x + pad, min.y + pad));
    ImGui::BeginGroup();
    ImGui::PushFont(theme_.heading, 26.0f);
    ImGui::TextUnformatted(resultTitle(out).c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s, %s", out.reason.c_str(), resultToString(out.result).c_str());
    if (ratedGen_ == generation_ && isRated() && game_.ply() >= 2) {
        const ImVec4 col = lastRatedDelta_ > 0 ? c.qBest : lastRatedDelta_ < 0 ? c.qBlunder : c.textMuted;
        ImGui::TextColored(col, "%s Elo %d  (%s%d)", static_cast<const char*>(icon::rating), player_.rating,
                           lastRatedDelta_ >= 0 ? "+" : "", lastRatedDelta_);
    } else if (bot_) {
        ImGui::TextDisabled("%s Ungewertet: %s", static_cast<const char*>(icon::unrated), unratedReason().c_str());
    }
    ImGui::Dummy(ImVec2(0, 6 * s));
    if (primaryButton((std::string(icon::analysis) + "  Analysieren").c_str())) {
        startAnalysis();
        mode_ = Mode::Analysis;
        resultDismissed_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string(icon::refresh) + "  Neue Partie").c_str())) startNewGame();
    ImGui::SameLine();
    if (iconButton("##closecard", icon::close, "Schließen")) resultDismissed_ = true;
    ImGui::EndGroup();
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
    if (!settings_.save(env::settingsFile())) setStatus("Einstellungen konnten nicht gespeichert werden");
}

void GuiApp::applyTheme() {
    theme_.dark = settings_.darkTheme;
    theme_.apply();
}

void GuiApp::tabSettings() {
    const float s = theme_.scale;
    const ImVec4 accentSoft(theme_.p().accent.x, theme_.p().accent.y, theme_.p().accent.z, 0.30f);
    bool changed = false;
    auto option = [&](const char* label, bool* value, const char* help) {
        changed |= ImGui::Checkbox(label, value);
        if (help && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", help);
    };

    ImGui::BeginChild("##settings", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::Dummy(ImVec2(0, 2 * s));
    ImGui::TextDisabled("Schnellwahl");
    struct Preset {
        const char* label;
        AssistSettings value;
        const char* help;
    };
    const Preset presets[] = {
        {"Lernen", AssistSettings::learning(), "Alle Hilfen an, auch hängende Figuren"},
        {"Standard", AssistSettings::standard(), "Die üblichen Hilfen"},
        {"Ohne Hilfen", AssistSettings::none(), "Wie im Turnier: keine Punkte, Tipps, Zurücknehmen oder Bewertungen"},
    };
    const float w3 = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        const bool active = settings_.sameAssists(presets[i].value);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, accentSoft);
        if (ImGui::Button(presets[i].label, ImVec2(w3, 0))) {
            const bool dark = settings_.darkTheme, coords = settings_.coordinates;
            settings_ = presets[i].value;
            settings_.darkTheme = dark;
            settings_.coordinates = coords;
            changed = true;
        }
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", presets[i].help);
    }

    ImGui::SeparatorText("Beim Ziehen");
    option("Mögliche Züge anzeigen (Punkte)", &settings_.legalMoves,
           "Nach dem Anklicken einer Figur erscheinen Punkte auf allen Feldern, wohin sie ziehen kann.");
    ImGui::BeginDisabled(!settings_.legalMoves);
    ImGui::Indent();
    option("Gefährliche Zielfelder rot", &settings_.dangerTargets,
           "Punkte werden rot, wenn die Figur dort Material verliert: ungedeckt angegriffen oder von einer "
           "billigeren Figur schlagbar. Braucht „Mögliche Züge“.");
    ImGui::Unindent();
    ImGui::EndDisabled();
    option("Zielfeld unter der Maus umrahmen", &settings_.targetFrame, nullptr);

    ImGui::SeparatorText("Während der Partie");
    option("Gefährdete eigene Figuren markieren", &settings_.hangingPieces,
           "Roter Rahmen um eigene Figuren, die gerade Material verlieren: ungedeckt angegriffen oder von "
           "einer billigeren Figur bedroht (z. B. Dame von Springer).");
    option("Schach am König hervorheben", &settings_.checkGlow, nullptr);
    option("Letzten Zug hervorheben", &settings_.lastMove, nullptr);
    option("Materialvorteil anzeigen (+2)", &settings_.material, nullptr);
    option("Tipp-Funktion (H)", &settings_.hintButton, "Die Engine zeigt auf Wunsch einen guten Zug.");
    option("Zurücknehmen erlauben", &settings_.undo, nullptr);
    option("Bewertung des Bots anzeigen", &settings_.botInfo,
           "Begründung und Stellungsbewertung nach jedem Bot-Zug – verrät, wer besser steht.");
    option("Eigene Pfeile & Kreise (Rechtsklick)", &settings_.annotations, nullptr);

    ImGui::SeparatorText("Analyse");
    option("Bewertungsbalken neben dem Brett", &settings_.evalBar, nullptr);
    option("Besten Zug als Pfeil zeigen", &settings_.bestArrow, nullptr);
    option("?!  ?  ??  in der Zugliste", &settings_.moveMarks, nullptr);

    ImGui::SeparatorText("Darstellung");
    option("Partie nach dem Ende automatisch analysieren", &settings_.autoAnalyse, nullptr);
    option("Koordinaten am Brettrand", &settings_.coordinates, nullptr);
    if (ImGui::Checkbox("Dunkles Design", &settings_.darkTheme)) {
        applyTheme();
        changed = true;
    }

    ImGui::Dummy(ImVec2(0, 4 * s));
    ImGui::TextDisabled("Wird automatisch gespeichert.");
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
    ImGui::SeparatorText("Stärke");
    bool useElo = p.elo > 0;
    if (ImGui::Checkbox("Auf Elo begrenzen", &useElo)) {
        p.elo = useElo ? 1500 : 0;
        changed = true;
    }
    if (useElo)
        changed |= ImGui::SliderInt("##elo", &p.elo, 1320, 3190, "Elo %d");
    else
        changed |= ImGui::SliderInt("##skill", &p.skill, 0, 20, "Stufe %d / 20");
    if (!stockfish_) ImGui::TextDisabled("Stärke wirkt nur mit Stockfish.");

    ImGui::SeparatorText("Spielstil");
    float aggr = static_cast<float>(p.aggression);
    if (ImGui::SliderFloat("##aggr", &aggr, -1.0f, 1.0f, aggr < -0.05f ? "vorsichtig  %.2f"
                                                          : aggr > 0.05f  ? "aggressiv  %.2f"
                                                                          : "ausgeglichen  %.2f")) {
        p.aggression = aggr;
        changed = true;
    }
    tip("Aggressiv bevorzugt Schläge, Schachs und Vorstöße; vorsichtig meidet sie.");

    ImGui::SeparatorText("Zufall & Fehler");
    float rnd = static_cast<float>(p.randomness);
    if (ImGui::SliderFloat("##rnd", &rnd, 0.0f, 200.0f, "Zufall  %.0f cp")) {
        p.randomness = rnd;
        changed = true;
    }
    tip("Rauschen auf die Bewertung: höher = unberechenbarer.");
    float blunder = static_cast<float>(p.blunderChance * 100.0);
    if (ImGui::SliderFloat("##blunder", &blunder, 0.0f, 50.0f, "Patzer-Wahrscheinlichkeit  %.0f %%")) {
        p.blunderChance = blunder / 100.0;
        changed = true;
    }
    tip("So oft spielt der Bot absichtlich einen zufälligen Zug.");
    changed |= ImGui::SliderInt("##cand", &p.candidates, 1, 10, "%d Kandidatenzüge");
    tip("Aus so vielen guten Zügen wählt der Bot nach Stil und Zufall.");
    changed |= ImGui::SliderInt("##window", &p.window, 0, 400, "Toleranz  %d cp");
    tip("So viel schlechter als der beste Zug darf ein Kandidat höchstens sein.");

    ImGui::SeparatorText("Tempo");
    int mode = static_cast<int>(p.tempo.mode);
    if (ImGui::Combo("##tempo", &mode, "Sofort\0Fest\0Wie ein Mensch\0")) {
        p.tempo.mode = static_cast<TempoMode>(mode);
        changed = true;
    }
    tip("Wie ein Mensch: zufällige Bedenkzeit, schneller bei klaren Zügen, länger in komplizierten Stellungen.");
    if (p.tempo.mode != TempoMode::Instant) {
        float lo = p.tempo.minMs / 1000.0f, hi = p.tempo.maxMs / 1000.0f;
        if (p.tempo.mode == TempoMode::Fixed) {
            if (ImGui::SliderFloat("##fixed", &lo, 0.1f, 30.0f, "%.1f s pro Zug")) {
                p.tempo.minMs = static_cast<int>(lo * 1000);
                p.tempo.maxMs = std::max(p.tempo.maxMs, p.tempo.minMs);
                changed = true;
            }
        } else {
            if (ImGui::DragFloatRange2("##range", &lo, &hi, 0.05f, 0.1f, 60.0f, "min %.1f s", "max %.1f s")) {
                p.tempo.minMs = static_cast<int>(lo * 1000);
                p.tempo.maxMs = std::max(p.tempo.minMs, static_cast<int>(hi * 1000));
                changed = true;
            }
            float var = static_cast<float>(p.tempo.variance);
            if (ImGui::SliderFloat("##var", &var, 0.0f, 1.5f, "Schwankung  %.2f")) {
                p.tempo.variance = var;
                changed = true;
            }
            tip("Wie unterschiedlich lange der Bot nachdenkt.");
        }
    }
    float pressure = static_cast<float>(p.pressureBlunder * 100.0);
    if (ImGui::SliderFloat("##pressure", &pressure, 0.0f, 100.0f, "Nervosität in Zeitnot  %.0f %%")) {
        p.pressureBlunder = pressure / 100.0;
        changed = true;
    }
    tip("Unter 10 Sekunden auf der Uhr steigt die Fehlerwahrscheinlichkeit um bis zu diesen Wert.");
    ImGui::PopItemWidth();

    ImGui::Dummy(ImVec2(0, 2 * s));
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    if (ImGui::Button("Speichern", ImVec2(half, 0))) {
        const fs::path file = env::userBotsDir() / ((p.id.empty() ? std::string("eigener_bot") : p.id) + ".json");
        std::ofstream out(file, std::ios::binary);
        out << botProfileToJson(p);
        setStatus(out ? "Bot gespeichert: " + file.filename().string() : "Speichern fehlgeschlagen");
    }
    tip("Speichert nach Dokumente\\ChessEngine\\Bots – überschreibt die Vorlage beim nächsten Start.");
    ImGui::SameLine();
    if (ImGui::Button("Zurücksetzen", ImVec2(half, 0))) {
        reloadProfiles();
        changed = true;
    }

    if (changed && selectedProfile_ == activeProfile_ && bot_) botProfileDirty_ = true;
}

void GuiApp::tabExport() {
    const float s = theme_.scale;
    ImGui::BeginChild("##exp", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::Dummy(ImVec2(0, 2 * s));
    ImGui::TextDisabled("PGN – alle Züge als Text");
    std::string pgn = pgnText();
    ImGui::InputTextMultiline("##pgn", pgn.data(), pgn.size() + 1, ImVec2(-FLT_MIN, 200 * s),
                              ImGuiInputTextFlags_ReadOnly);
    const float w = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    if (ImGui::Button("Kopieren", ImVec2(w, 0))) {
        ImGui::SetClipboardText(pgn.c_str());
        setStatus("PGN in die Zwischenablage kopiert");
    }
    ImGui::SameLine();
    if (ImGui::Button("Speichern", ImVec2(w, 0))) savePgn(false);
    ImGui::SameLine();
    if (ImGui::Button("Ordner öffnen", ImVec2(w, 0))) {
        std::string url = "file:///" + env::gamesDir().generic_string();
        SDL_OpenURL(url.c_str());
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Tipp: Auf lichess.org/paste einfügen – dort gibt es eine kostenlose Computeranalyse.");
    ImGui::PopTextWrapPos();

    ImGui::Dummy(ImVec2(0, 6 * s));
    ImGui::Separator();
    ImGui::TextDisabled("FEN der angezeigten Stellung");
    std::string fen = toFen(viewedPosition());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90 * s);
    ImGui::InputText("##fen", fen.data(), fen.size() + 1, ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Kopieren##fen", ImVec2(-FLT_MIN, 0))) {
        ImGui::SetClipboardText(fen.c_str());
        setStatus("FEN kopiert");
    }

    ImGui::TextDisabled("Stellung laden (FEN)");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90 * s);
    ImGui::InputText("##fenin", fenInput_, sizeof(fenInput_));
    ImGui::SameLine();
    if (ImGui::Button("Laden", ImVec2(-FLT_MIN, 0))) loadFen(fenInput_);
    ImGui::EndChild();
}

}  // namespace chess::ui
