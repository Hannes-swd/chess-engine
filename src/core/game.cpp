#include "core/game.hpp"

#include "core/fen.hpp"
#include "core/notation.hpp"

#include <algorithm>

namespace chess {

const char* const kStandardStartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

Variant standardVariant() {
    Variant v;
    v.name = "Standard";
    v.pieces = PieceRegistry::standard();
    v.startFen = kStandardStartFen;
    return v;
}

namespace {

Position makeStart(Variant& v) {
    if (!v.pieces) v.pieces = PieceRegistry::standard();
    if (v.startFen.empty()) v.startFen = kStandardStartFen;
    Position p = fromFen(v.pieces, v.startFen);
    for (const auto& e : v.hooks.onSetup.entries()) e.fn(p);
    return p;
}

bool insufficientMaterial(const Position& pos) {
    int minors = 0;
    for (Square s = 0; s < pos.squareCount(); ++s) {
        const Piece& p = pos.at(s);
        if (p.empty()) continue;
        const PieceType& t = pos.registry().get(p.type);
        if (t.royal) continue;
        if (t.id == "knight" || t.id == "bishop") {
            ++minors;
            continue;
        }
        return false;
    }
    return minors <= 1;
}

}  // namespace

bool hasMatingMaterial(const Position& pos, Color c) {
    int minors = 0;
    for (Square s = 0; s < pos.squareCount(); ++s) {
        const Piece& p = pos.at(s);
        if (p.empty() || p.color != c) continue;
        const PieceType& t = pos.registry().get(p.type);
        if (t.royal) continue;
        if (t.id == "knight" || t.id == "bishop") {
            ++minors;
            continue;
        }
        return true;  // Bauer, Turm, Dame, Plugin-Figur
    }
    return minors >= 2;
}

Game::Game(Variant variant) : variant_(std::move(variant)), start_(makeStart(variant_)), current_(start_) {
    keys_.push_back(current_.repetitionKey());
}

void Game::reset() {
    start_ = makeStart(variant_);
    current_ = start_;
    history_.clear();
    moves_.clear();
    san_.clear();
    keys_.clear();
    keys_.push_back(current_.repetitionKey());
    declared_.reset();
}

std::vector<Move> Game::legalMoves() const {
    std::vector<Move> moves = current_.legalMoves();
    for (const auto& e : variant_.hooks.filterMoves.entries()) e.fn(current_, moves);
    return moves;
}

bool Game::play(const Move& requested) {
    if (outcome().over()) return false;
    const std::vector<Move> legal = legalMoves();
    const auto it = std::find(legal.begin(), legal.end(), requested);
    if (it == legal.end()) return false;
    const Move m = *it;  // mit vollstaendigen Flags

    Position before = current_;
    std::string san = toSan(before, m);

    history_.push_back(current_);
    current_.makeMove(m);
    for (const auto& e : variant_.hooks.onMovePlayed.entries()) e.fn(current_, m);

    moves_.push_back(m);
    san_.push_back(std::move(san));
    keys_.push_back(current_.repetitionKey());
    return true;
}

bool Game::play(std::string_view text) {
    Position tmp = current_;
    const auto m = parseMove(tmp, legalMoves(), text);
    return m && play(*m);
}

bool Game::undo() {
    if (history_.empty()) return false;
    declared_.reset();
    current_ = std::move(history_.back());
    history_.pop_back();
    moves_.pop_back();
    san_.pop_back();
    keys_.pop_back();
    return true;
}

const Position& Game::positionAt(int ply) const {
    if (ply < 0 || ply >= static_cast<int>(history_.size())) return current_;
    return history_[static_cast<size_t>(ply)];
}

int Game::repetitionCount() const {
    return static_cast<int>(std::count(keys_.begin(), keys_.end(), keys_.back()));
}

GameOutcome Game::outcome() const {
    if (declared_) return *declared_;
    for (const auto& e : variant_.hooks.checkGameEnd.entries())
        if (auto r = e.fn(*this)) return *r;

    if (!variant_.standardEndRules) return {};

    const Color side = current_.sideToMove();
    if (legalMoves().empty()) {
        if (current_.inCheck(side))
            return {side == Color::White ? GameResult::BlackWins : GameResult::WhiteWins, "Schachmatt"};
        return {GameResult::Draw, "Patt"};
    }
    if (current_.halfmoveClock() >= 100) return {GameResult::Draw, "50-Züge-Regel"};
    if (repetitionCount() >= 3) return {GameResult::Draw, "Dreifache Stellungswiederholung"};
    if (insufficientMaterial(current_)) return {GameResult::Draw, "Ungenügendes Material"};
    return {};
}

}  // namespace chess
