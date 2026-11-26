#include "core/pgn.hpp"

#include "core/fen.hpp"

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

}  // namespace chess
