#include "core/fen.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace chess {

namespace {

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> splitWhitespace(std::string_view s) {
    std::istringstream in{std::string(s)};
    std::vector<std::string> out;
    std::string tok;
    while (in >> tok) out.push_back(tok);
    return out;
}

bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

int rowWidth(const std::string& row) {
    int w = 0;
    int num = 0;
    for (char c : row) {
        if (isDigit(c)) {
            num = num * 10 + (c - '0');
        } else {
            w += num + 1;
            num = 0;
        }
    }
    return w + num;
}

// Erster rochadefaehiger Koenig einer Farbe.
Square findCastlingKing(const Position& pos, Color c) {
    for (Square s = 0; s < pos.squareCount(); ++s) {
        const Piece& p = pos.at(s);
        if (!p.empty() && p.color == c && pos.registry().get(p.type).castles) return s;
    }
    return NoSquare;
}

// Aeusserster Rochadepartner in Richtung `dir` auf dem Rang des Koenigs.
Square findCastlePartner(const Position& pos, Square king, int dir, bool mustBeUnmoved) {
    const Piece& k = pos.at(king);
    const int rank = pos.rankOf(king);
    Square found = NoSquare;
    for (int f = pos.fileOf(king) + dir; pos.contains(f, rank); f += dir) {
        const Square q = pos.square(f, rank);
        const Piece& p = pos.at(q);
        if (p.empty() || p.color != k.color || !pos.registry().get(p.type).castlePartner) continue;
        if (mustBeUnmoved && !pos.isUnmoved(q)) continue;
        found = q;
    }
    return found;
}

int parseInt(const std::string& s, const char* what) {
    try {
        size_t used = 0;
        const int v = std::stoi(s, &used);
        if (used != s.size()) throw std::invalid_argument(s);
        return v;
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("Ungültige Zahl im FEN (") + what + "): " + s);
    }
}

}  // namespace

std::string squareName(const Position& pos, Square s) {
    std::string r;
    r += static_cast<char>('a' + pos.fileOf(s));
    r += std::to_string(pos.rankOf(s) + 1);
    return r;
}

std::optional<Square> parseSquare(const Position& pos, std::string_view text) {
    if (text.size() < 2) return std::nullopt;
    const char f = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
    if (f < 'a' || f > 'z') return std::nullopt;
    int rank = 0;
    for (size_t i = 1; i < text.size(); ++i) {
        if (!isDigit(text[i])) return std::nullopt;
        rank = rank * 10 + (text[i] - '0');
    }
    if (rank < 1) return std::nullopt;
    const int file = f - 'a';
    if (!pos.contains(file, rank - 1)) return std::nullopt;
    return pos.square(file, rank - 1);
}

Position fromFen(std::shared_ptr<const PieceRegistry> registry, std::string_view fen) {
    const auto fields = splitWhitespace(fen);
    if (fields.empty()) throw std::invalid_argument("Leerer FEN");

    const auto rows = split(fields[0], '/');
    const int height = static_cast<int>(rows.size());
    const int width = rowWidth(rows[0]);
    for (const auto& row : rows)
        if (rowWidth(row) != width) throw std::invalid_argument("FEN: Ränge haben unterschiedliche Breite");

    Position pos(registry, width, height);

    for (int i = 0; i < height; ++i) {
        const int rank = height - 1 - i;
        int file = 0;
        int num = 0;
        for (char c : rows[static_cast<size_t>(i)]) {
            if (isDigit(c)) {
                num = num * 10 + (c - '0');
                continue;
            }
            file += num;
            num = 0;
            const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            const auto id = registry->findBySymbol(upper);
            if (!id) throw std::invalid_argument(std::string("FEN: unbekannte Figur '") + c + "'");
            const Color col = std::isupper(static_cast<unsigned char>(c)) ? Color::White : Color::Black;
            pos.place(pos.square(file, rank), Piece{*id, col});
            ++file;
        }
    }

    if (fields.size() > 1) {
        if (fields[1] == "w")
            pos.setSideToMove(Color::White);
        else if (fields[1] == "b")
            pos.setSideToMove(Color::Black);
        else
            throw std::invalid_argument("FEN: Seite am Zug muss 'w' oder 'b' sein");
    }

    if (fields.size() > 2 && fields[2] != "-") {
        for (char c : fields[2]) {
            const Color col = std::isupper(static_cast<unsigned char>(c)) ? Color::White : Color::Black;
            const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (u != 'K' && u != 'Q') throw std::invalid_argument(std::string("FEN: unbekanntes Rochaderecht '") + c + "'");
            const Square king = findCastlingKing(pos, col);
            if (king == NoSquare) continue;
            const Square rook = findCastlePartner(pos, king, u == 'K' ? 1 : -1, false);
            if (rook == NoSquare) continue;
            pos.setUnmoved(king, true);
            pos.setUnmoved(rook, true);
        }
    }

    if (fields.size() > 3 && fields[3] != "-") {
        const auto ep = parseSquare(pos, fields[3]);
        if (!ep) throw std::invalid_argument("FEN: ungültiges en-passant-Feld");
        pos.setEnPassant(*ep);
    }

    if (fields.size() > 4) pos.setHalfmoveClock(parseInt(fields[4], "Halbzüge"));
    if (fields.size() > 5) pos.setFullmoveNumber(parseInt(fields[5], "Zugnummer"));
    return pos;
}

std::string toFen(const Position& pos) {
    std::string out;
    for (int rank = pos.height() - 1; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < pos.width(); ++file) {
            const Piece& p = pos.at(pos.square(file, rank));
            if (p.empty()) {
                ++empty;
                continue;
            }
            if (empty) {
                out += std::to_string(empty);
                empty = 0;
            }
            const char sym = pos.registry().get(p.type).symbol;
            out += p.color == Color::White ? sym : static_cast<char>(std::tolower(static_cast<unsigned char>(sym)));
        }
        if (empty) out += std::to_string(empty);
        if (rank > 0) out += '/';
    }

    out += pos.sideToMove() == Color::White ? " w " : " b ";

    std::string castling;
    for (Color col : {Color::White, Color::Black}) {
        const Square king = findCastlingKing(pos, col);
        if (king == NoSquare || !pos.isUnmoved(king)) continue;
        if (findCastlePartner(pos, king, 1, true) != NoSquare) castling += col == Color::White ? 'K' : 'k';
        if (findCastlePartner(pos, king, -1, true) != NoSquare) castling += col == Color::White ? 'Q' : 'q';
    }
    out += castling.empty() ? "-" : castling;

    out += ' ';
    out += pos.enPassant() == NoSquare ? "-" : squareName(pos, pos.enPassant());
    out += ' ';
    out += std::to_string(pos.halfmoveClock());
    out += ' ';
    out += std::to_string(pos.fullmoveNumber());
    return out;
}

}  // namespace chess
