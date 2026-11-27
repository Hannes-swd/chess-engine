// Spielgefuehl und Kleinigkeiten: Klaenge, Remis anbieten, Partie beim naechsten Start fortsetzen,
// eigene Bots anlegen, Stockfish nachtraeglich finden.

#include "ui/gui_app.hpp"
#include "ui/i18n.hpp"

#include "app/environment.hpp"
#include "core/notation.hpp"
#include "core/pgn.hpp"
#include "ui/gui_internal.hpp"
#include "util/json.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace chess::ui {

namespace fs = std::filesystem;

namespace {

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20) out += c;
        }
    }
    return out + "\"";
}

}  // namespace

// ===========================================================================
// Klaenge
// ===========================================================================

void GuiApp::playSound(Sound::Kind kind) {
    if (sandbox_ || !settings_.sounds) return;
    if (!soundTried_) {  // Audio erst beim ersten Klang starten
        soundTried_ = true;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) soundReady_ = sound_.init();
    }
    if (soundReady_) sound_.play(kind);
}

void GuiApp::playSounds(const Position& before, const Move& m) {
    (void)before;
    if (game_.position().inCheck(game_.position().sideToMove()))
        playSound(Sound::Kind::Check);
    else if (m.flags & FlagCapture)
        playSound(Sound::Kind::Capture);
    else
        playSound(Sound::Kind::Move);
}

// ===========================================================================
// Remis anbieten
// ===========================================================================

void GuiApp::offerDraw() {
    if (game_.outcome().over() || game_.ply() < 2) return;
    if (!bot_) {  // zu zweit am Brett: beide sitzen ja davor
        game_.setResult({GameResult::Draw, tr("Remis vereinbart")});
        view_ = -1;
        return;
    }
    if (game_.ply() - drawOfferPly_ < 10) {
        setStatus(tr("Gerade erst angeboten – ein paar Züge warten"));
        return;
    }
    drawOfferPly_ = game_.ply();
    // Der Bot nimmt an, wenn er schlechter steht, oder nach vielen Zuegen in einer toten Stellung
    bool accept = false;
    if (lastBotScore_) {
        const Score& sc = *lastBotScore_;
        const int cp = sc.mate ? (sc.winning ? 10000 : -10000) : sc.value;
        accept = cp <= -150 || (game_.ply() >= 60 && std::abs(cp) <= 30);
    }
    if (accept) {
        pendingBot_.reset();
        game_.setResult({GameResult::Draw, tr("Remis vereinbart")});
        view_ = -1;
    } else {
        setStatus(bot_->profile().displayName(english()) + tr(" lehnt das Remis ab"));
    }
}

// ===========================================================================
// Partie fortsetzen
// ===========================================================================

void GuiApp::saveResume() {
    if (sandbox_ || game_.outcome().over() || game_.ply() == 0 || pluginGame_ || !loadedTags_.empty()) return;
    std::string out = "{\n";
    out += "  \"pgn\": " + jsonString(toPgn(game_)) + ",\n";
    out += "  \"profile\": " + jsonString(bot_ ? bot_->profile().id : std::string()) + ",\n";
    out += "  \"botColor\": \"" + std::string(botColor_ == Color::White ? "white" : "black") + "\",\n";
    out += "  \"tcBase\": " + std::to_string(clock_.control().baseMs) + ",\n";
    out += "  \"tcInc\": " + std::to_string(clock_.control().incrementMs) + ",\n";
    out += "  \"whiteMs\": " + std::to_string(clock_.remaining(Color::White)) + ",\n";
    out += "  \"blackMs\": " + std::to_string(clock_.remaining(Color::Black)) + ",\n";
    out += "  \"undoUsed\": " + std::string(undoUsed_ ? "true" : "false") + ",\n";
    out += "  \"hintUsed\": " + std::string(hintUsed_ ? "true" : "false") + ",\n";
    out += "  \"fenGame\": " + std::string(fenGame_ ? "true" : "false") + "\n}\n";
    std::ofstream file(env::resumeFile(), std::ios::binary);
    file << out;
}

void GuiApp::loadResume() {
    std::ifstream in(env::resumeFile(), std::ios::binary);
    if (!in) return;
    std::stringstream ss;
    ss << in.rdbuf();
    const auto v = json::parse(ss.str());
    if (!v || !v->isObject()) return;
    const auto rec = parsePgn(v->getString("pgn", ""));
    if (!rec || rec->moves.empty()) return;
    resumeText_ = ss.str();
    std::string opponent = tr("zu zweit");
    for (const BotProfile& p : profiles_)
        if (p.id == v->getString("profile", "")) opponent = tr("gegen ") + p.displayName(english());
    const int moves = static_cast<int>(rec->moves.size() + 1) / 2;
    resumeInfo_ = tr("Partie ") + opponent + ", " + std::to_string(moves) + (moves == 1 ? tr(" Zug") : tr(" Züge"));
}

void GuiApp::resumeGame() {
    const std::string text = resumeText_;  // startNewGame verwirft die Fortsetzung
    const auto v = json::parse(text);
    if (!v || !v->isObject()) return;
    const auto rec = parsePgn(v->getString("pgn", ""));
    if (!rec) return;

    selectedProfile_ = -1;
    for (size_t i = 0; i < profiles_.size(); ++i)
        if (profiles_[i].id == v->getString("profile", "")) selectedProfile_ = static_cast<int>(i);
    colorChoice_ = v->getString("botColor", "black") == "black" ? 0 : 1;
    const int base = static_cast<int>(v->getNumber("tcBase", 0)), inc = static_cast<int>(v->getNumber("tcInc", 0));
    if (base <= 0) {
        tcPreset_ = 0;
    } else {
        tcPreset_ = detail::kTimePresetCount - 1;  // eigene Bedenkzeit
        customMinutes_ = static_cast<float>(base / 60000.0);
        customIncrement_ = inc / 1000;
    }
    const std::vector<std::string> plugins = settings_.plugins;
    settings_.plugins.clear();  // fortgesetzt wird immer normales Schach
    startNewGame();
    settings_.plugins = plugins;

    for (const std::string& san : rec->moves) {
        Position tmp = game_.position();
        const auto m = parseMove(tmp, game_.legalMoves(), san);
        if (!m || !game_.play(*m)) break;
    }
    clock_.restore({static_cast<int>(v->getNumber("whiteMs", base)), static_cast<int>(v->getNumber("blackMs", base))});
    clockHistory_.assign(static_cast<size_t>(game_.ply()), clock_.snapshot());
    undoUsed_ = v->getBool("undoUsed", false);
    hintUsed_ = v->getBool("hintUsed", false);
    fenGame_ = v->getBool("fenGame", false);
    lastTick_ = std::chrono::steady_clock::now();
    mode_ = Mode::Play;
    playTab_ = 1;
    setStatus(tr("Partie fortgesetzt"));
}

// ===========================================================================
// Eigene Bots, Stockfish
// ===========================================================================

void GuiApp::createOwnBot() {
    BotProfile p = selectedProfile_ >= 0 && selectedProfile_ < static_cast<int>(profiles_.size())
                       ? profiles_[static_cast<size_t>(selectedProfile_)]
                       : (profiles_.empty() ? BotProfile{} : profiles_.front());
    int n = 1;
    auto taken = [&](const std::string& id) {
        return std::any_of(profiles_.begin(), profiles_.end(), [&](const BotProfile& q) { return q.id == id; });
    };
    while (taken("eigener_bot_" + std::to_string(n))) ++n;
    p.id = "eigener_bot_" + std::to_string(n);
    p.name = n == 1 ? tr("Mein Bot") : tr("Mein Bot ") + std::to_string(n);
    p.description = tr("Eigener Bot – Name, Stärke und Stil unten anpassen und speichern.");
    if (!sandbox_) {
        std::ofstream out(env::userBotsDir() / (p.id + ".json"), std::ios::binary);
        out << botProfileToJson(p);
    }
    profiles_.push_back(p);
    selectedProfile_ = static_cast<int>(profiles_.size()) - 1;
    mode_ = Mode::Bots;
    setStatus(tr("Neuer Bot angelegt: ") + p.name);
}

void GuiApp::retryStockfish() {
    if (stockfish_) return;
    if (auto sf = env::findStockfish(exeDir_)) {
        std::string err;
        stockfish_ = UciEngine::launch(*sf, err);
        if (!stockfish_) {
            setStatus(tr("Stockfish konnte nicht starten: ") + err);
            return;
        }
        engineInfo_ = stockfish_->name();
        analysisDepth_ = 14;
        setStatus(tr("Stockfish gefunden: ") + engineInfo_);
    } else {
        setStatus(tr("Stockfish nicht gefunden – stockfish.exe in den Ordner engines legen"));
    }
}

}  // namespace chess::ui
