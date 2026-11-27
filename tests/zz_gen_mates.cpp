// VORUEBERGEHEND: erzeugt Matt-in-1-Stellungen mit genau einer Loesung fuer das Plugin "matt_in_eins".
#include "framework.hpp"

#include "app/environment.hpp"
#include "core/fen.hpp"
#include "core/notation.hpp"
#include "core/openings.hpp"
#include "engine/uci_engine.hpp"

#include <cctype>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>

using namespace chess;

namespace {

std::vector<Move> matingMoves(const Position& pos) {
    std::vector<Move> out;
    for (const Move& m : pos.legalMoves()) {
        Position p = pos;
        p.makeMove(m);
        if (p.inCheck(p.sideToMove()) && p.legalMoves().empty()) out.push_back(m);
    }
    return out;
}

int checkCount(const Position& pos) {
    int n = 0;
    for (const Move& m : pos.legalMoves()) {
        Position p = pos;
        p.makeMove(m);
        if (p.inCheck(p.sideToMove())) ++n;
    }
    return n;
}

// Farben tauschen und Brett spiegeln: Schwarz am Zug -> Weiss am Zug
std::string mirrorFen(const std::string& fen) {
    std::istringstream in(fen);
    std::string board, side, castle, ep, half, full;
    in >> board >> side >> castle >> ep >> half >> full;
    std::vector<std::string> rows;
    std::stringstream rs(board);
    for (std::string r; std::getline(rs, r, '/');) rows.push_back(r);
    std::string nb;
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        std::string r = *it;
        for (char& c : r)
            if (std::isalpha(static_cast<unsigned char>(c)))
                c = std::isupper(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(c))
                                                                : static_cast<char>(std::toupper(c));
        nb += (nb.empty() ? "" : "/") + r;
    }
    std::string nc;
    for (char c : std::string("KQkq")) {
        const char src = std::isupper(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(c))
                                                                     : static_cast<char>(std::toupper(c));
        if (castle.find(src) != std::string::npos) nc += c;
    }
    if (nc.empty()) nc = "-";
    std::string ne = ep;
    if (ep != "-") ne = std::string(1, ep[0]) + static_cast<char>('9' - (ep[1] - '0'));
    return nb + " " + (side == "w" ? "b" : "w") + " " + nc + " " + ne + " " + half + " " + full;
}

std::string kindOf(const Position& pos, const Move& m) {
    const PieceType& t = pos.registry().get(pos.at(m.from).type);
    if (m.flags & FlagCastle) return "Rochade";
    if (m.flags & FlagEnPassant) return "en passant";
    if (m.flags & FlagPromotion) return "Umwandlung";
    Position after = pos;
    after.makeMove(m);
    const Color them = after.sideToMove();
    const Square king = after.royalSquares(them).front();
    // Erstickt: Springer, Koenig nur von eigenen Figuren umgeben
    if (t.id == "knight") {
        bool smothered = true;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy) {
                const int x = after.fileOf(king) + dx, y = after.rankOf(king) + dy;
                if ((dx || dy) && after.contains(x, y)) {
                    const Piece& p = after.at(after.square(x, y));
                    if (p.empty() || p.color != them) smothered = false;
                }
            }
        if (smothered) return "erstickt";
    }
    // Abzug: die gezogene Figur gibt selbst kein Schach
    {
        Position probe = after;
        probe.clear(m.to);
        if (probe.inCheck(them)) {
            Position solo = after;
            // pruefen, ob die Figur auf m.to allein Schach gibt: andere Angreifer entfernen ist aufwendig,
            // daher: Schach bleibt ohne die gezogene Figur -> Abzugs- oder Doppelschach
            return "Abzug";
        }
    }
    if ((t.id == "rook" || t.id == "queen") && after.rankOf(king) == (them == Color::Black ? 7 : 0) &&
        after.rankOf(m.to) == after.rankOf(king))
        return "Grundreihe";
    return t.id;
}

}  // namespace

TEST_CASE(zz_generate_mates) {
    const auto path = env::findStockfish(env::executableDir());
    CHECK(path.has_value());
    if (!path) return;
    std::string err;
    auto sf = UciEngine::launch(*path, err);
    if (!sf) return;
    std::mt19937 rng(42);
    std::map<std::string, std::vector<std::string>> byKind;
    std::set<std::string> seen;
    const auto& ops = openings();
    for (int gameNo = 0; gameNo < 4500; ++gameNo) {
        Game g;
        const Opening& o = ops[rng() % ops.size()];
        for (const auto& m : o.moves) g.play(m);
        sf->configureStrength(static_cast<int>(rng() % 5), 0);
        sf->newGame();
        while (!g.outcome().over() && g.ply() < 180) {
            const Position pos = g.position();
            const auto mates = matingMoves(pos);
            if (mates.size() == 1 && pos.legalMoves().size() >= 10) {
                std::string fen = toFen(pos);
                if (pos.sideToMove() == Color::Black) fen = mirrorFen(fen);
                Position w = fromFen(PieceRegistry::standard(), fen);
                const auto wm = matingMoves(w);
                if (wm.size() == 1 && !w.inCheck(Color::White)) {
                    const std::string key = fen.substr(0, fen.find(' '));
                    const std::string kind = kindOf(w, wm.front());
                    const int checks = checkCount(w);
                    const bool rare = kind != "queen" && kind != "rook";
                    if ((checks >= 2 || rare) && seen.insert(key).second) {
                        Position tmp = w;
                        byKind[kind].push_back(fen + "\t" + toSan(tmp, wm.front()) + "\t" + kind + "\t" +
                                               std::to_string(checks));
                    }
                }
            }
            SearchLimits lim;
            lim.depth = 1 + static_cast<int>(rng() % 4);
            const SearchResult r = sf->search(pos, lim);
            if (!r.ok || !r.bestMove.valid() || !g.play(r.bestMove)) break;
        }
    }
    std::ofstream out(R"(C:\Users\hanne\AppData\Local\Temp\claude\51699e6c68099484\33174d99-4fef-4e36-8b2c-ed9c63d0bb6a\scratchpad\mates.txt)");
    int total = 0;
    for (auto& [kind, list] : byKind) {
        std::shuffle(list.begin(), list.end(), rng);
        const size_t take = std::min<size_t>(list.size(), 30);
        std::cout << "  " << kind << ": " << list.size() << " gefunden, " << take << " genommen\n";
        for (size_t i = 0; i < take; ++i) out << list[i] << "\n";
        total += static_cast<int>(take);
    }
    std::cout << "  gesamt " << total << "\n";
    CHECK(total > 50);
}
