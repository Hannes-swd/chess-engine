#include "core/pgn.hpp"

#include "core/fen.hpp"
#include "core/notation.hpp"

#include <stdexcept>

namespace chess {

std::string resultToString(GameResult r) {
    switch (r) {
        case GameResult::WhiteWins: return "1-0";
        case GameResult::BlackWins: return "0-1";
        case GameResult::Draw: return "1/2-1/2";
        case GameResult::Ongoing: break;
    }
    return "*";
}

std::string toPgn(const Game& game, const PgnTags& extra, const std::vector<std::string>* moveComments) {
    const std::string result = resultToString(game.outcome().result);
    const std::string startFen = toFen(game.startPosition());
    const bool customStart = startFen != kStandardStartFen;

    PgnTags tags = {
        {"Event", "Lokale Partie"}, {"Site", "ChessEngine"}, {"Date", "????.??.??"}, {"Round", "-"},
        {"White", "Weiss"},         {"Black", "Schwarz"},    {"Result", result},
    };
    if (game.variant().name != "Standard") tags.emplace_back("Variant", game.variant().name);
    if (customStart) {
        tags.emplace_back("SetUp", "1");
        tags.emplace_back("FEN", startFen);
    }
    for (const auto& [k, v] : extra) {
        bool replaced = false;
        for (auto& [tk, tv] : tags)
            if (tk == k) {
                tv = v;
                replaced = true;
            }
        if (!replaced) tags.emplace_back(k, v);
    }

    std::string out;
    for (const auto& [k, v] : tags) out += "[" + k + " \"" + v + "\"]\n";
    out += '\n';

    // Zugtext, umgebrochen bei ca. 80 Zeichen
    std::string line;
    auto emit = [&](const std::string& token) {
        if (!line.empty() && line.size() + 1 + token.size() > 80) {
            out += line + '\n';
            line.clear();
        }
        if (!line.empty()) line += ' ';
        line += token;
    };

    int number = game.startPosition().fullmoveNumber();
    Color side = game.startPosition().sideToMove();
    const auto& san = game.sanMoves();
    // Ein schwarzer Zug braucht eine Nummer ("3...") am Anfang und nach einem Kommentar.
    bool needNumber = true;
    for (size_t i = 0; i < san.size(); ++i) {
        // Zugnummer und Zug als ein Token, damit der Umbruch sie nicht trennt
        if (side == Color::White)
            emit(std::to_string(number) + ". " + san[i]);
        else if (needNumber)
            emit(std::to_string(number) + "... " + san[i]);
        else
            emit(san[i]);
        needNumber = false;
        if (moveComments && i < moveComments->size() && !(*moveComments)[i].empty()) {
            emit("{" + (*moveComments)[i] + "}");
            needNumber = true;
        }
        if (side == Color::Black) ++number;
        side = opposite(side);
    }
    emit(result);
    out += line + '\n';
    return out;
}

// ===========================================================================
// Einlesen
// ===========================================================================

std::string PgnRecord::tag(const std::string& key) const {
    for (const auto& [k, v] : tags)
        if (k == key) return v;
    return {};
}

namespace {

bool isResultToken(std::string_view t) { return t == "1-0" || t == "0-1" || t == "1/2-1/2" || t == "*"; }

// "12." / "12..." / "12" -> reine Zugnummer
bool isMoveNumber(std::string_view t) {
    size_t i = 0;
    while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i;
    if (i == 0) return false;
    while (i < t.size() && t[i] == '.') ++i;
    return i == t.size();
}

}  // namespace

std::optional<PgnRecord> parsePgn(std::string_view text) {
    PgnRecord rec;
    size_t i = 0;
    const size_t n = text.size();
    bool inMoves = false;
    std::string token;

    auto flush = [&]() {
        if (token.empty()) return;
        std::string t = token;
        token.clear();
        // Zugnummer direkt vor dem Zug ("12.e4", "12...Nf6")
        size_t dot = t.find_last_of('.');
        if (dot != std::string::npos && dot + 1 < t.size() && isMoveNumber(t.substr(0, dot + 1)))
            t = t.substr(dot + 1);
        if (isMoveNumber(t) || isResultToken(t) || t[0] == '$') return;
        while (!t.empty() && (t.back() == '!' || t.back() == '?')) t.pop_back();
        if (!t.empty()) rec.moves.push_back(std::move(t));
    };

    while (i < n) {
        const char ch = text[i];
        if (!inMoves && ch == '[') {  // Tag-Zeile: [Name "Wert"]
            const size_t end = text.find(']', i);
            if (end == std::string_view::npos) break;
            const std::string_view inner = text.substr(i + 1, end - i - 1);
            const size_t sp = inner.find(' ');
            const size_t q1 = inner.find('"');
            const size_t q2 = inner.rfind('"');
            if (sp != std::string_view::npos && q1 != std::string_view::npos && q2 > q1) {
                std::string value;
                for (size_t k = q1 + 1; k < q2; ++k) {
                    if (inner[k] == '\\' && k + 1 < q2) ++k;
                    value += inner[k];
                }
                rec.tags.emplace_back(std::string(inner.substr(0, sp)), std::move(value));
            }
            i = end + 1;
            continue;
        }
        if (ch == '{') {  // Kommentar: gehoert zum Zug davor (enthaelt z. B. die Bewertung)
            flush();
            const size_t end = text.find('}', i);
            const size_t stop = end == std::string_view::npos ? n : end;
            if (!rec.moves.empty()) {
                rec.comments.resize(rec.moves.size());
                std::string& c = rec.comments.back();
                if (!c.empty()) c += ' ';
                c += std::string(text.substr(i + 1, stop - i - 1));
            }
            i = stop == n ? n : stop + 1;
            continue;
        }
        if (ch == ';') {  // Kommentar bis Zeilenende
            flush();
            const size_t end = text.find('\n', i);
            i = end == std::string_view::npos ? n : end + 1;
            continue;
        }
        if (ch == '(') {  // Variante (verschachtelt) ueberspringen
            flush();
            int depth = 0;
            for (; i < n; ++i) {
                if (text[i] == '{') {
                    const size_t end = text.find('}', i);
                    if (end == std::string_view::npos) break;
                    i = end;
                } else if (text[i] == '(') {
                    ++depth;
                } else if (text[i] == ')' && --depth == 0) {
                    break;
                }
            }
            ++i;
            continue;
        }
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            flush();
            ++i;
            continue;
        }
        inMoves = true;
        // Ergebnis beendet die Partie (eine weitere Partie in derselben Datei wird ignoriert)
        token += ch;
        ++i;
        if (isResultToken(token) && (i >= n || text[i] == ' ' || text[i] == '\r' || text[i] == '\n')) {
            token.clear();
            break;
        }
    }
    flush();
    rec.comments.resize(rec.moves.size());
    if (rec.tags.empty() && rec.moves.empty()) return std::nullopt;
    return rec;
}

Game gameFromPgn(const PgnRecord& record) {
    Variant v = standardVariant();
    const std::string fen = record.tag("FEN");
    if (!fen.empty()) v.startFen = fen;
    Game game(v);
    for (const std::string& san : record.moves) {
        Position tmp = game.position();
        const auto m = parseMove(tmp, game.legalMoves(), san);
        if (!m || !game.play(*m))
            throw std::runtime_error("Zug " + std::to_string(game.ply() / 2 + 1) + " nicht lesbar: " + san);
    }
    if (!game.outcome().over()) {
        const std::string r = record.tag("Result");
        const std::string why = record.tag("Termination").empty() ? "Ergebnis aus der Datei" : record.tag("Termination");
        if (r == "1-0") game.setResult({GameResult::WhiteWins, why});
        if (r == "0-1") game.setResult({GameResult::BlackWins, why});
        if (r == "1/2-1/2") game.setResult({GameResult::Draw, why});
    }
    return game;
}

}  // namespace chess
