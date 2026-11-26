#include "core/notation.hpp"

#include "core/fen.hpp"

#include <cctype>

namespace chess {

namespace {

std::string normalize(std::string_view s) {
    std::string r;
    for (char c : s) {
        if (c == '+' || c == '#' || c == '!' || c == '?' || std::isspace(static_cast<unsigned char>(c))) continue;
        r += c;
    }
    if (r.rfind("0-0", 0) == 0)
        for (char& c : r)
            if (c == '0') c = 'O';
    return r;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

std::string toUci(const Position& pos, const Move& m) {
    std::string s = squareName(pos, m.from) + squareName(pos, m.to);
    if (m.promotion != NoPieceType)
        s += static_cast<char>(std::tolower(static_cast<unsigned char>(pos.registry().get(m.promotion).symbol)));
    return s;
}

std::string toSan(Position& pos, const Move& m) {
    std::string san;
    const Piece moved = pos.at(m.from);
    const PieceType& t = pos.registry().get(moved.type);
    const bool capture = (m.flags & (FlagCapture | FlagEnPassant)) != 0;

    if (m.flags & FlagCastle) {
        san = pos.fileOf(m.rookFrom) > pos.fileOf(m.from) ? "O-O" : "O-O-O";
    } else if (t.pawnLike) {
        if (capture) {
            san += static_cast<char>('a' + pos.fileOf(m.from));
            san += 'x';
        }
        san += squareName(pos, m.to);
        if (m.promotion != NoPieceType) {
            san += '=';
            san += pos.registry().get(m.promotion).symbol;
        }
    } else {
        san += t.symbol;

        std::vector<Move> legal;
        pos.generateLegal(legal);
        bool ambiguous = false;
        bool sameFile = false;
        bool sameRank = false;
        for (const Move& o : legal) {
            if (o.from == m.from || o.to != m.to) continue;
            if (pos.at(o.from).type != moved.type) continue;
            ambiguous = true;
            if (pos.fileOf(o.from) == pos.fileOf(m.from)) sameFile = true;
            if (pos.rankOf(o.from) == pos.rankOf(m.from)) sameRank = true;
        }
        if (ambiguous) {
            if (!sameFile)
                san += static_cast<char>('a' + pos.fileOf(m.from));
            else if (!sameRank)
                san += std::to_string(pos.rankOf(m.from) + 1);
            else
                san += squareName(pos, m.from);
        }
        if (capture) san += 'x';
        san += squareName(pos, m.to);
    }

    const Position::Undo u = pos.makeMove(m);
    if (pos.inCheck(pos.sideToMove())) {
        std::vector<Move> replies;
        pos.generateLegal(replies);
        san += replies.empty() ? '#' : '+';
    }
    pos.unmakeMove(m, u);
    return san;
}

std::optional<Move> parseMove(Position& pos, const std::vector<Move>& candidates, std::string_view text) {
    const std::string want = normalize(text);
    if (want.empty()) return std::nullopt;

    for (const Move& m : candidates)
        if (normalize(toSan(pos, m)) == want) return m;

    const std::string wantLower = lower(want);
    for (const Move& m : candidates)
        if (toUci(pos, m) == wantLower) return m;

    return std::nullopt;
}

std::optional<Move> parseMove(Position& pos, std::string_view text) {
    std::vector<Move> legal;
    pos.generateLegal(legal);
    return parseMove(pos, legal, text);
}

std::optional<Move> parseUciMove(Position& pos, std::string_view text) {
    const std::string want = lower(std::string(text));
    std::vector<Move> legal;
    pos.generateLegal(legal);
    for (const Move& m : legal)
        if (toUci(pos, m) == want) return m;
    return std::nullopt;
}

}  // namespace chess
