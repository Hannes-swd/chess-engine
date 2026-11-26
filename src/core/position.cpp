#include "core/position.hpp"

#include <cstdlib>
#include <stdexcept>

namespace chess {

Position::Position(std::shared_ptr<const PieceRegistry> registry, int width, int height)
    : registry_(std::move(registry)) {
    if (!registry_) throw std::invalid_argument("Position braucht eine PieceRegistry");
    resize(width, height);
}

void Position::resize(int width, int height) {
    if (width < 1 || height < 1 || width > 26 || height > 99)
        throw std::invalid_argument("Brettgröße muss zwischen 1x1 und 26x99 liegen");
    width_ = width;
    height_ = height;
    board_.assign(static_cast<size_t>(width * height), Piece{});
    unmoved_.assign(static_cast<size_t>(width * height), 0);
    enPassant_ = NoSquare;
}

void Position::place(Square s, Piece p, bool unmoved) {
    board_[static_cast<size_t>(s)] = p;
    unmoved_[static_cast<size_t>(s)] = unmoved ? 1 : 0;
}

void Position::clear(Square s) { place(s, Piece{}, false); }

// ---------------------------------------------------------------------------
// Zuggenerierung
// ---------------------------------------------------------------------------

void Position::generatePseudoLegal(std::vector<Move>& out) const {
    for (Square s = 0; s < squareCount(); ++s) {
        const Piece& p = board_[static_cast<size_t>(s)];
        if (p.empty() || p.color != side_) continue;
        const PieceType& t = registry_->get(p.type);
        generateSteps(s, p, t, out);
        if (t.castles) generateCastling(s, p, out);
        if (t.customMoves) t.customMoves(*this, s, out);
    }
}

void Position::addMove(Square from, Square to, uint8_t flags, const Piece& p, const PieceType& t,
                       std::vector<Move>& out) const {
    if (t.pawnLike && rankOf(to) == promotionRank(p.color)) {
        const auto& types = registry_->all();
        for (PieceTypeId id = 0; id < static_cast<PieceTypeId>(types.size()); ++id) {
            if (!types[static_cast<size_t>(id)].promotionTarget) continue;
            out.push_back(Move{from, to, id, static_cast<uint8_t>(flags | FlagPromotion)});
        }
        return;
    }
    out.push_back(Move{from, to, NoPieceType, flags});
}

void Position::generateSteps(Square from, const Piece& p, const PieceType& t, std::vector<Move>& out) const {
    const int f0 = fileOf(from);
    const int r0 = rankOf(from);
    const int fwd = forward(p.color);

    for (const Step& st : t.steps) {
        const int dx = st.dx;
        const int dy = st.dy * fwd;
        int f = f0 + dx;
        int r = r0 + dy;
        while (contains(f, r)) {
            const Square to = square(f, r);
            const Piece& q = board_[static_cast<size_t>(to)];
            if (q.empty()) {
                if (st.mode != StepMode::CaptureOnly) {
                    addMove(from, to, FlagNone, p, t, out);
                    // Doppelschritt vom Startrang
                    if (t.pawnLike && st.mode == StepMode::MoveOnly && !st.slide && dx == 0 &&
                        r0 == pawnStartRank(p.color)) {
                        const int r2 = r + dy;
                        if (contains(f, r2) && board_[static_cast<size_t>(square(f, r2))].empty())
                            out.push_back(Move{from, square(f, r2), NoPieceType, FlagDoublePush});
                    }
                } else if (t.pawnLike && to == enPassant_) {
                    out.push_back(Move{from, to, NoPieceType, static_cast<uint8_t>(FlagCapture | FlagEnPassant)});
                }
            } else {
                if (q.color != p.color && st.mode != StepMode::MoveOnly) addMove(from, to, FlagCapture, p, t, out);
                break;
            }
            if (!st.slide) break;
            f += dx;
            r += dy;
        }
    }
}

void Position::generateCastling(Square from, const Piece& p, std::vector<Move>& out) const {
    if (!isUnmoved(from)) return;
    const Color them = opposite(p.color);
    if (isAttacked(from, them)) return;

    const int f0 = fileOf(from);
    const int r0 = rankOf(from);
    for (int dir : {1, -1}) {
        // Der erste Stein in Richtung `dir` muss der Rochadepartner sein -> alles dazwischen ist leer.
        Square rookSq = NoSquare;
        for (int f = f0 + dir; contains(f, r0); f += dir) {
            const Square q = square(f, r0);
            if (!board_[static_cast<size_t>(q)].empty()) {
                rookSq = q;
                break;
            }
        }
        if (rookSq == NoSquare) continue;
        const Piece& rook = board_[static_cast<size_t>(rookSq)];
        if (rook.color != p.color || !registry_->get(rook.type).castlePartner || !isUnmoved(rookSq)) continue;
        if (std::abs(fileOf(rookSq) - f0) < 3) continue;

        const Square pass = square(f0 + dir, r0);
        const Square kingTo = square(f0 + 2 * dir, r0);
        if (isAttacked(pass, them)) continue;  // Zielfeld prueft der Legalitaetsfilter

        Move m{from, kingTo, NoPieceType, FlagCastle};
        m.rookFrom = rookSq;
        m.rookTo = pass;
        out.push_back(m);
    }
}

void Position::generateLegal(std::vector<Move>& out) {
    std::vector<Move> pseudo;
    pseudo.reserve(64);
    generatePseudoLegal(pseudo);
    const Color us = side_;
    for (const Move& m : pseudo) {
        const Undo u = makeMove(m);
        if (!inCheck(us)) out.push_back(m);
        unmakeMove(m, u);
    }
}

std::vector<Move> Position::legalMoves() const {
    Position copy = *this;
    std::vector<Move> out;
    copy.generateLegal(out);
    return out;
}

// ---------------------------------------------------------------------------
// Angriffe
// ---------------------------------------------------------------------------

bool Position::isAttacked(Square s, Color by) const {
    const int f0 = fileOf(s);
    const int r0 = rankOf(s);
    const int fwd = forward(by);
    const auto& types = registry_->all();

    for (PieceTypeId id = 0; id < static_cast<PieceTypeId>(types.size()); ++id) {
        const PieceType& t = types[static_cast<size_t>(id)];

        // Rueckwaerts suchen: steht entlang des umgekehrten Schritts eine passende Figur?
        for (const Step& st : t.steps) {
            if (st.mode == StepMode::MoveOnly) continue;
            const int dx = -st.dx;
            const int dy = -st.dy * fwd;
            int f = f0 + dx;
            int r = r0 + dy;
            while (contains(f, r)) {
                const Piece& q = board_[static_cast<size_t>(square(f, r))];
                if (!q.empty()) {
                    if (q.type == id && q.color == by) return true;
                    break;
                }
                if (!st.slide) break;
                f += dx;
                r += dy;
            }
        }

        if (t.customMoves) {
            std::vector<Move> tmp;
            for (Square q = 0; q < squareCount(); ++q) {
                const Piece& p = board_[static_cast<size_t>(q)];
                if (p.type != id || p.color != by) continue;
                tmp.clear();
                t.customMoves(*this, q, tmp);
                for (const Move& m : tmp)
                    if (m.to == s) return true;
            }
        }
    }
    return false;
}

std::vector<Square> Position::royalSquares(Color c) const {
    std::vector<Square> out;
    for (Square s = 0; s < squareCount(); ++s) {
        const Piece& p = board_[static_cast<size_t>(s)];
        if (!p.empty() && p.color == c && registry_->get(p.type).royal) out.push_back(s);
    }
    return out;
}

bool Position::inCheck(Color c) const {
    const Color them = opposite(c);
    for (Square s = 0; s < squareCount(); ++s) {
        const Piece& p = board_[static_cast<size_t>(s)];
        if (!p.empty() && p.color == c && registry_->get(p.type).royal && isAttacked(s, them)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Zug ausfuehren / zuruecknehmen
// ---------------------------------------------------------------------------

Position::Undo Position::makeMove(const Move& m) {
    Undo u;
    u.moved = board_[static_cast<size_t>(m.from)];
    u.prevEnPassant = enPassant_;
    u.prevHalfmove = halfmove_;
    u.capturedSq = (m.flags & FlagEnPassant) ? square(fileOf(m.to), rankOf(m.from)) : m.to;
    u.captured = board_[static_cast<size_t>(u.capturedSq)];

    auto save = [&](Square sq) {
        if (sq == NoSquare) return;
        for (int i = 0; i < u.unmovedCount; ++i)
            if (u.unmovedSaved[static_cast<size_t>(i)].first == sq) return;
        u.unmovedSaved[static_cast<size_t>(u.unmovedCount++)] = {sq, unmoved_[static_cast<size_t>(sq)]};
    };
    save(m.from);
    save(m.to);
    save(u.capturedSq);
    save(m.rookFrom);
    save(m.rookTo);

    board_[static_cast<size_t>(u.capturedSq)] = Piece{};
    unmoved_[static_cast<size_t>(u.capturedSq)] = 0;

    Piece placed = u.moved;
    if (m.promotion != NoPieceType) placed.type = m.promotion;
    board_[static_cast<size_t>(m.from)] = Piece{};
    board_[static_cast<size_t>(m.to)] = placed;
    unmoved_[static_cast<size_t>(m.from)] = 0;
    unmoved_[static_cast<size_t>(m.to)] = 0;

    if (m.flags & FlagCastle) {
        const Piece rook = board_[static_cast<size_t>(m.rookFrom)];
        board_[static_cast<size_t>(m.rookFrom)] = Piece{};
        board_[static_cast<size_t>(m.rookTo)] = rook;
        unmoved_[static_cast<size_t>(m.rookFrom)] = 0;
        unmoved_[static_cast<size_t>(m.rookTo)] = 0;
    }

    enPassant_ = (m.flags & FlagDoublePush) ? square(fileOf(m.from), (rankOf(m.from) + rankOf(m.to)) / 2)
                                            : NoSquare;

    if (registry_->get(u.moved.type).pawnLike || !u.captured.empty())
        halfmove_ = 0;
    else
        ++halfmove_;

    if (side_ == Color::Black) ++fullmove_;
    side_ = opposite(side_);
    return u;
}

void Position::unmakeMove(const Move& m, const Undo& u) {
    side_ = opposite(side_);
    if (side_ == Color::Black) --fullmove_;

    if (m.flags & FlagCastle) {
        const Piece rook = board_[static_cast<size_t>(m.rookTo)];
        board_[static_cast<size_t>(m.rookTo)] = Piece{};
        board_[static_cast<size_t>(m.rookFrom)] = rook;
    }

    board_[static_cast<size_t>(m.to)] = Piece{};
    board_[static_cast<size_t>(m.from)] = u.moved;
    board_[static_cast<size_t>(u.capturedSq)] = u.captured;

    for (int i = u.unmovedCount - 1; i >= 0; --i) {
        const auto& [sq, v] = u.unmovedSaved[static_cast<size_t>(i)];
        unmoved_[static_cast<size_t>(sq)] = v;
    }

    enPassant_ = u.prevEnPassant;
    halfmove_ = u.prevHalfmove;
}

std::string Position::repetitionKey() const {
    std::string key;
    key.reserve(static_cast<size_t>(squareCount()) * 2 + 16);
    key += std::to_string(width_);
    key += 'x';
    key += std::to_string(height_);
    key += ':';
    for (Square s = 0; s < squareCount(); ++s) {
        const Piece& p = board_[static_cast<size_t>(s)];
        if (p.empty()) {
            key += '.';
            continue;
        }
        key += static_cast<char>('A' + (p.type % 26));
        key += p.color == Color::White ? 'w' : 'b';
        // Rochaderechte gehoeren zur Stellung.
        const PieceType& t = registry_->get(p.type);
        if ((t.castles || t.castlePartner) && isUnmoved(s)) key += '*';
    }
    key += side_ == Color::White ? 'W' : 'B';
    key += std::to_string(enPassant_);
    return key;
}

}  // namespace chess
