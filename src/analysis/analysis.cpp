#include "analysis/analysis.hpp"

#include "core/notation.hpp"
#include "core/tactics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chess {

// ===========================================================================
// Grundlagen
// ===========================================================================

std::string qualityName(MoveQuality q) {
    switch (q) {
        case MoveQuality::Brilliant: return "Brillant";
        case MoveQuality::Great: return "Sehr gut";
        case MoveQuality::Best: return "Bester Zug";
        case MoveQuality::Excellent: return "Exzellent";
        case MoveQuality::Good: return "Gut";
        case MoveQuality::Inaccuracy: return "Ungenau";
        case MoveQuality::Mistake: return "Fehler";
        case MoveQuality::Miss: return "Verpasst";
        case MoveQuality::Blunder: return "Patzer";
        case MoveQuality::Forced: return "Einziger Zug";
    }
    return "?";
}

bool isError(MoveQuality q) {
    return q == MoveQuality::Inaccuracy || q == MoveQuality::Mistake || q == MoveQuality::Miss ||
           q == MoveQuality::Blunder;
}

double winChance(const Score& s) {
    if (s.mate) return s.winning ? 100.0 : 0.0;
    const double cp = std::clamp(s.value, -1500, 1500);
    return 50.0 + 50.0 * (2.0 / (1.0 + std::exp(-0.00368208 * cp)) - 1.0);
}

double moveAccuracy(double winBefore, double winAfter) {
    const double drop = std::max(0.0, winBefore - winAfter);
    return std::clamp(103.1668 * std::exp(-0.04354 * drop) - 3.1669, 0.0, 100.0);
}

std::string lineText(const Position& start, const std::vector<Move>& moves, size_t maxPlies) {
    Position p = start;
    std::string out;
    for (size_t i = 0; i < moves.size() && i < maxPlies; ++i) {
        const auto legal = p.legalMoves();
        if (std::find(legal.begin(), legal.end(), moves[i]) == legal.end()) break;
        if (p.sideToMove() == Color::White) {
            if (!out.empty()) out += ' ';
            out += std::to_string(p.fullmoveNumber()) + ". ";
        } else if (i == 0) {
            out += std::to_string(p.fullmoveNumber()) + "... ";
        } else {
            out += ' ';
        }
        out += toSan(p, moves[i]);
        p.makeMove(moves[i]);
    }
    if (moves.size() > maxPlies && !out.empty()) out += " …";
    return out;
}

namespace {

std::vector<std::string> sanLine(const Position& start, const std::vector<Move>& moves) {
    Position p = start;
    std::vector<std::string> out;
    for (const Move& m : moves) {
        const auto legal = p.legalMoves();
        if (std::find(legal.begin(), legal.end(), m) == legal.end()) break;
        out.push_back(toSan(p, m));
        p.makeMove(m);
    }
    return out;
}

const char* colorName(Color c) { return c == Color::White ? "Weiß" : "Schwarz"; }

// Satzende: kein zusaetzlicher Punkt nach "…"
std::string endSentence(const std::string& s) {
    if (s.size() >= 3 && s.compare(s.size() - 3, 3, "\xE2\x80\xA6") == 0) return s;
    return s + ".";
}

std::string pawns(int cp) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", std::abs(cp) / 100.0);
    std::string s = buf;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

// Welche Figur wird mit diesem Zug geschlagen (oder nullptr)?
const PieceType* capturedBy(const Position& pos, const Move& m) {
    if (!(m.flags & FlagCapture)) return nullptr;
    const Square sq = (m.flags & FlagEnPassant) ? pos.square(pos.fileOf(m.to), pos.rankOf(m.from)) : m.to;
    const Piece& p = pos.at(sq);
    return p.empty() ? nullptr : &pos.registry().get(p.type);
}

// Niedrigste Materialbilanz (aus Sicht von `me`) entlang einer Zugfolge, jeweils nach Zuegen des Gegners.
int worstBalanceAlong(Position p, const std::vector<Move>& line, Color me, size_t maxPlies) {
    int worst = materialBalance(p, me);
    for (size_t i = 0; i < line.size() && i < maxPlies; ++i) {
        const auto legal = p.legalMoves();
        if (std::find(legal.begin(), legal.end(), line[i]) == legal.end()) break;
        const bool opponentMoves = p.sideToMove() != me;
        p.makeMove(line[i]);
        if (opponentMoves) worst = std::min(worst, materialBalance(p, me));
    }
    return worst;
}

// Opfer: Nach dem Zug (und der besten Antwort) hat der Ziehende spuerbar weniger Material als vorher.
bool detectSacrifice(const Position& before, const Move& played, const std::vector<Move>& threatLine, int minLoss) {
    const Color me = before.sideToMove();
    const int start = materialBalance(before, me);
    Position after = before;
    after.makeMove(played);
    const int afterMove = materialBalance(after, me);

    // a) Die gezogene Figur kann mit Gewinn fuer den Gegner geschlagen werden
    const Piece& moved = after.at(played.to);
    if (!moved.empty()) {
        const PieceType& t = after.registry().get(moved.type);
        if (!t.royal && t.value >= 300 && isEnPrise(after, played.to) && afterMove - t.value <= start - minLoss)
            return true;
    }
    // b) Entlang der besten Antwort geht Material verloren (geplante Kombination)
    return worstBalanceAlong(after, threatLine, me, 3) <= start - minLoss;
}

// Englische Figurennamen (Objekt bzw. Satzanfang)
std::string pieceObjectEn(const PieceType& t) {
    if (t.id == "pawn") return "a pawn";
    if (t.id == "knight") return "a knight";
    if (t.id == "bishop") return "a bishop";
    if (t.id == "rook") return "a rook";
    if (t.id == "queen") return "the queen";
    if (t.id == "king") return "the king";
    return "the piece \"" + t.name + "\"";
}

std::string pieceSubjectEn(const PieceType& t) {
    if (t.id == "pawn") return "The pawn";
    if (t.id == "knight") return "The knight";
    if (t.id == "bishop") return "The bishop";
    if (t.id == "rook") return "The rook";
    if (t.id == "queen") return "The queen";
    if (t.id == "king") return "The king";
    return "The piece \"" + t.name + "\"";
}

std::string pawnsEn(int cp) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", std::abs(cp) / 100.0);
    return buf;
}

// Ueberschrift und Erklaerung in einer Sprache (en = Englisch)
void explainIn(MoveReview& r, const Position& before, const Position& after, const MoveReview* prev, bool en,
               std::string& headline, std::string& explanation) {
    auto L = [en](const char* de, const char* eng) { return std::string(en ? eng : de); };
    const std::string me = en ? (r.mover == Color::White ? "White" : "Black") : colorName(r.mover);
    const std::string opp = en ? (r.mover == Color::White ? "Black" : "White") : colorName(opposite(r.mover));
    const PieceRegistry& reg = before.registry();
    auto object = [&](const PieceType& t) { return en ? pieceObjectEn(t) : pieceAccusative(t); };
    auto subject = [&](const PieceType& t) { return en ? pieceSubjectEn(t) : pieceNominative(t); };
    auto amount = [&](int cp) { return en ? pawnsEn(cp) : pawns(cp); };

    switch (r.quality) {
        case MoveQuality::Brilliant: headline = r.san + L(" ist brillant", " is brilliant"); break;
        case MoveQuality::Great: headline = r.san + L(" ist ein sehr guter Zug", " is a great move"); break;
        case MoveQuality::Best: headline = r.san + L(" ist der beste Zug", " is the best move"); break;
        case MoveQuality::Excellent: headline = r.san + L(" ist exzellent", " is excellent"); break;
        case MoveQuality::Good: headline = r.san + L(" ist gut", " is good"); break;
        case MoveQuality::Inaccuracy: headline = r.san + L(" ist ungenau", " is an inaccuracy"); break;
        case MoveQuality::Mistake: headline = r.san + L(" ist ein Fehler", " is a mistake"); break;
        case MoveQuality::Miss: headline = r.san + L(" verpasst eine Chance", " misses a chance"); break;
        case MoveQuality::Blunder: headline = r.san + L(" ist ein Patzer", " is a blunder"); break;
        case MoveQuality::Forced: headline = r.san + L(" war der einzige Zug", " was the only move"); break;
    }

    std::string text;
    const std::string bestLine = lineText(before, r.bestLine, 6);
    const std::string threat = lineText(after, r.threatLine, 5);

    switch (r.quality) {
        case MoveQuality::Brilliant: {
            const Piece& moved = after.at(r.played.to);
            const std::string what = moved.empty() ? L("Material", "material") : object(reg.get(moved.type));
            text = en ? me + " sacrifices " + what + ". " + opp +
                            " can take it, but is not better afterwards – the sacrifice is calculated."
                      : me + " opfert " + what + ". " + opp +
                            " kann zugreifen, steht danach aber nicht besser – das Opfer ist durchgerechnet.";
            if (!threat.empty()) text += L(" Die Idee: ", " The idea: ") + endSentence(threat);
            break;
        }
        case MoveQuality::Great: {
            const int gap = r.bestScore.clamped(1500) - (r.secondScore ? r.secondScore->clamped(1500) : 0);
            text = en ? "Only this move holds the position. The next best move would be " + amount(gap) +
                            " pawns worse."
                      : "Nur dieser Zug hält die Stellung. Der nächstbeste Zug wäre um " + amount(gap) +
                            " Bauerneinheiten schlechter.";
            if (!threat.empty()) text += L(" Weiter geht es mit ", " It continues with ") + endSentence(threat);
            break;
        }
        case MoveQuality::Best:
        case MoveQuality::Excellent:
        case MoveQuality::Good: {
            if (r.playedScore.mate && r.playedScore.winning && r.playedScore.value == 0)
                text = L("Schachmatt.", "Checkmate.");
            else if (r.playedScore.mate && r.playedScore.winning)
                text = en ? "Mate in " + std::to_string(r.playedScore.value) + " can no longer be stopped."
                          : "Danach ist das Matt in " + std::to_string(r.playedScore.value) + " nicht mehr aufzuhalten.";
            else if (const PieceType* cap = capturedBy(before, r.played))
                text = L("Schlägt ", "Captures ") + object(*cap) + ".";
            if (r.quality == MoveQuality::Best) {
                if (text.empty()) text = L("Genau der Zug, den die Engine auch wählt.", "Exactly the move the engine picks too.");
            } else {
                text += (text.empty() ? "" : " ") + (r.quality == MoveQuality::Excellent
                                                         ? L("Fast so stark wie ", "Almost as strong as ")
                                                         : L("Ordentlich. Etwas genauer war ", "Decent. A bit more precise was ")) +
                        r.bestSan + ".";
            }
            break;
        }
        case MoveQuality::Forced: text = L("Es gab keinen anderen legalen Zug.", "There was no other legal move."); break;
        default: {
            // Warum war es schlecht?
            if (r.playedScore.mate && !r.playedScore.winning) {
                const std::string n = std::to_string(std::max(1, r.playedScore.value));
                text = en ? "Now " + opp + " can mate in " + n + (n == "1" ? " move" : " moves")
                          : "Danach kann " + opp + " in " + n + " Zügen mattsetzen";
                text += threat.empty() ? std::string(".") : ": " + endSentence(threat);
            } else {
                const int lossAlongThreat =
                    materialBalance(after, r.mover) - worstBalanceAlong(after, r.threatLine, r.mover, 3);
                const PieceType* cap = r.threatLine.empty() ? nullptr : capturedBy(after, r.threatLine.front());
                if (cap && lossAlongThreat >= 100 && !r.threatLineSan.empty()) {
                    text = en ? opp + " then wins " + object(*cap) + " with " + r.threatLineSan.front() + "."
                              : opp + " gewinnt danach " + object(*cap) + " mit " + r.threatLineSan.front() + ".";
                } else {
                    // Steht eine eigene Figur danach ungedeckt?
                    for (Square s = 0; s < after.squareCount() && text.empty(); ++s) {
                        const Piece& p = after.at(s);
                        if (p.empty() || p.color != r.mover || reg.get(p.type).royal) continue;
                        if (isEnPrise(after, s) && !isEnPrise(before, s == r.played.to ? r.played.from : s)) {
                            const std::string sq =
                                std::string(1, char('a' + after.fileOf(s))) + std::to_string(after.rankOf(s) + 1);
                            text = en ? subject(reg.get(p.type)) + " on " + sq + " is left hanging."
                                      : subject(reg.get(p.type)) + " auf " + sq + " hängt danach.";
                        }
                    }
                }
                if (text.empty()) {
                    char buf[200];
                    if (en)
                        std::snprintf(buf, sizeof(buf), "%s's winning chances drop from %.0f %% to %.0f %%.", me.c_str(),
                                      r.winBefore, r.winAfter);
                    else
                        std::snprintf(buf, sizeof(buf), "Die Gewinnchance für %s sinkt von %.0f %% auf %.0f %%.",
                                      me.c_str(), r.winBefore, r.winAfter);
                    text = buf;
                }
                if (!threat.empty() && text.find(threat) == std::string::npos)
                    text += (en ? " " + opp + "'s best reply: " : " Die beste Antwort von " + opp + ": ") +
                            endSentence(threat);
            }
            if (r.quality == MoveQuality::Miss && prev) {
                std::string chance;
                if (r.bestScore.mate && r.bestScore.winning)
                    chance = en ? "mated in " + std::to_string(r.bestScore.value)
                                : "in " + std::to_string(r.bestScore.value) + " Zügen mattsetzen können";
                else if (const PieceType* c2 = r.best.valid() ? capturedBy(before, r.best) : nullptr)
                    chance = en ? "won " + object(*c2) : object(*c2) + " gewinnen können";
                if (en)
                    text = opp + " had just made a mistake with " + prev->san + ". " +
                           (chance.empty() ? std::string("The chance was not used. ")
                                           : "With " + r.bestSan + " you could have " + chance + ". ") +
                           text;
                else
                    text = opp + " hatte gerade mit " + prev->san + " einen Fehler gemacht. " +
                           (chance.empty() ? std::string("Die Gelegenheit blieb ungenutzt. ")
                                           : "Mit " + r.bestSan + " hättest du " + chance + ". ") +
                           text;
            }
            if (!r.bestSan.empty())
                text += L(" Besser war ", " Better was ") + r.bestSan + " (" + r.bestScore.toString() + ")" +
                        (bestLine.empty() ? std::string(".") : ": " + endSentence(bestLine));
            break;
        }
    }
    explanation = text;
}

void explain(MoveReview& r, const Position& before, const Position& after, const MoveReview* prev) {
    explainIn(r, before, after, prev, false, r.headline, r.explanation);
    explainIn(r, before, after, prev, true, r.headlineEn, r.explanationEn);
}
}  // namespace

// ===========================================================================
// Analyse
// ===========================================================================

GameAnalysis analyseGame(const Game& game, Engine& engine, const SearchLimits& limits,
                         const QualityThresholds& th, const AnalysisProgress& progress) {
    GameAnalysis out;
    out.engineName = engine.name();
    const int n = game.ply();

    for (int i = 0; i <= n; ++i)
        if (!engine.supportsPosition(game.positionAt(i))) {
            out.error = engine.name() + " kennt diese Variante nicht";
            return out;
        }
    engine.configureStrength(20, 0);  // ein Bot hat die Engine evtl. geschwaecht

    // 1) Jede Stellung mit den zwei besten Zuegen bewerten
    std::vector<std::optional<SearchResult>> evals(static_cast<size_t>(n + 1));
    for (int i = 0; i <= n; ++i) {
        const Position& p = game.positionAt(i);
        if (!p.legalMoves().empty()) {
            SearchLimits lim = limits;
            lim.multiPv = 2;
            lim.rootMoves.clear();
            SearchResult r = engine.search(p, lim);
            if (!r.ok) {
                out.error = r.error.empty() ? "Engine-Fehler" : r.error;
                return out;
            }
            evals[static_cast<size_t>(i)] = std::move(r);
        }
        if (progress) progress(i + 1, n + 1);
        if (limits.stop && limits.stop->load()) {
            out.error = "Analyse abgebrochen";
            return out;
        }
    }

    // 2) Bewertungskurve aus Sicht von Weiss
    out.whiteEvals.resize(static_cast<size_t>(n + 1));
    for (int k = 0; k <= n; ++k) {
        const Position& p = game.positionAt(k);
        const Color stm = p.sideToMove();
        Score s;
        const auto& e = evals[static_cast<size_t>(k)];
        if (e && !e->lines.empty())
            s = e->lines.front().score;
        else if (p.legalMoves().empty() && p.inCheck(stm))
            s = Score::mateIn(0, false);
        out.whiteEvals[static_cast<size_t>(k)] = stm == Color::White ? s : s.negated();
    }

    // 3) Jeden Zug einstufen und erklaeren
    for (int i = 0; i < n; ++i) {
        const Position& before = game.positionAt(i);
        const Position& after = game.positionAt(i + 1);
        MoveReview r;
        r.ply = i;
        r.mover = before.sideToMove();
        r.san = game.sanMoves()[static_cast<size_t>(i)];
        r.played = game.moves()[static_cast<size_t>(i)];

        const auto& e = evals[static_cast<size_t>(i)];
        if (e && !e->lines.empty()) {
            r.best = e->bestMove.valid() ? e->bestMove : e->lines.front().move;
            r.bestScore = e->lines.front().score;
            r.bestLine = e->lines.front().pv.empty() ? std::vector<Move>{r.best} : e->lines.front().pv;
            if (e->lines.size() > 1) r.secondScore = e->lines[1].score;
            Position tmp = before;
            if (r.best.valid()) r.bestSan = toSan(tmp, r.best);
            r.bestLineSan = sanLine(before, r.bestLine);
        }
        const Score white = out.whiteEvals[static_cast<size_t>(i + 1)];
        r.playedScore = r.mover == Color::White ? white : white.negated();
        if (const auto& next = evals[static_cast<size_t>(i + 1)]; next && !next->lines.empty()) {
            r.threatLine = next->lines.front().pv.empty() ? std::vector<Move>{next->lines.front().move}
                                                          : next->lines.front().pv;
            r.threatLineSan = sanLine(after, r.threatLine);
        }

        r.cpLoss = std::max(0, r.bestScore.clamped(th.clamp) - r.playedScore.clamped(th.clamp));
        r.winBefore = winChance(r.bestScore);
        r.winAfter = winChance(r.playedScore);
        r.accuracy = moveAccuracy(r.winBefore, r.winAfter);
        const double drop = std::max(0.0, r.winBefore - r.winAfter);
        const bool isBest = (r.best.valid() && r.played == r.best) || r.cpLoss <= th.bestTolerance;

        if (before.legalMoves().size() == 1)
            r.quality = MoveQuality::Forced;
        else if (isBest)
            r.quality = MoveQuality::Best;
        else if (drop <= th.excellent)
            r.quality = MoveQuality::Excellent;
        else if (drop <= th.good)
            r.quality = MoveQuality::Good;
        else if (drop <= th.inaccuracy)
            r.quality = MoveQuality::Inaccuracy;
        else if (drop <= th.mistake)
            r.quality = MoveQuality::Mistake;
        else
            r.quality = MoveQuality::Blunder;

        // Besonders gute Zuege: nur wenn die Partie noch offen ist
        if (r.quality == MoveQuality::Best && r.winBefore < 90.0 && r.winAfter >= 40.0) {
            r.sacrifice = detectSacrifice(before, r.played, r.threatLine, th.sacrifice);
            if (r.sacrifice)
                r.quality = MoveQuality::Brilliant;
            else if (r.secondScore && r.bestScore.clamped(th.clamp) - r.secondScore->clamped(th.clamp) >= th.greatGap)
                r.quality = MoveQuality::Great;
        }

        // Verpasst: der Gegner hat gerade gepatzt, und die Chance wurde nicht genutzt
        const MoveReview* prev = out.moves.empty() ? nullptr : &out.moves.back();
        if (prev && (prev->quality == MoveQuality::Mistake || prev->quality == MoveQuality::Blunder) &&
            (r.quality == MoveQuality::Mistake || r.quality == MoveQuality::Blunder ||
             (r.quality == MoveQuality::Inaccuracy && r.winBefore >= 70.0)))
            r.quality = MoveQuality::Miss;

        explain(r, before, after, prev);
        out.moves.push_back(std::move(r));
    }

    // 4) Zusammenfassung pro Spieler und Vorteilsverteilung
    for (Color c : {Color::White, Color::Black}) {
        PlayerSummary& s = c == Color::White ? out.white : out.black;
        double acc = 0;
        int loss = 0;
        for (const MoveReview& r : out.moves) {
            if (r.mover != c) continue;
            ++s.moves;
            acc += r.accuracy;
            loss += r.cpLoss;
            ++s.counts[static_cast<size_t>(r.quality)];
        }
        if (s.moves) {
            s.accuracy = acc / s.moves;
            s.avgCpLoss = loss / s.moves;
        }
    }
    int w = 0, b = 0, eq = 0;
    for (const Score& s : out.whiteEvals) {
        const double wc = winChance(s);
        if (wc > 60.0)
            ++w;
        else if (wc < 40.0)
            ++b;
        else
            ++eq;
    }
    const double total = std::max(1, w + b + eq);
    out.whiteBetter = 100.0 * w / total;
    out.blackBetter = 100.0 * b / total;
    out.balanced = 100.0 * eq / total;

    out.ok = true;
    return out;
}

// ===========================================================================
// Ausgabe
// ===========================================================================

namespace {

Score whiteView(const MoveReview& r) { return r.mover == Color::White ? r.playedScore : r.playedScore.negated(); }

std::string pad(std::string s, size_t width) {
    size_t visible = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++visible;
    if (visible < width) s.append(width - visible, ' ');
    return s;
}

}  // namespace

std::vector<std::string> pgnComments(const GameAnalysis& a) {
    std::vector<std::string> out;
    for (const MoveReview& r : a.moves) {
        const bool matedNow = r.playedScore.mate && r.playedScore.value == 0;
        std::string c = matedNow ? qualityName(r.quality) + " – Matt"
                                 : "[%eval " + whiteView(r).toPgnEval() + "] " + qualityName(r.quality);
        if (isError(r.quality) && !r.bestSan.empty()) c += " – besser: " + r.bestSan;
        out.push_back(std::move(c));
    }
    return out;
}

std::string formatAnalysis(const Game& game, const GameAnalysis& a) {
    if (!a.ok) return "Analyse fehlgeschlagen: " + a.error + "\n";

    std::string out = "Analyse mit " + a.engineName + "\n\n";
    int number = game.startPosition().fullmoveNumber();
    for (const MoveReview& r : a.moves) {
        std::string label = std::to_string(number) + (r.mover == Color::White ? ". " : "... ") + r.san;
        std::string line = "  " + pad(label, 14) + pad(qualityName(r.quality), 14) + pad(whiteView(r).toString(), 9);
        if (isError(r.quality) && !r.bestSan.empty()) line += "besser: " + r.bestSan + " (" + r.bestScore.toString() + ")";
        out += line + "\n";
        if (r.mover == Color::Black) ++number;
    }

    out += "\n";
    for (Color c : {Color::White, Color::Black}) {
        const PlayerSummary& s = a.summary(c);
        if (s.moves == 0) continue;
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "%s Genauigkeit %.1f %%, Ø Verlust %d cp, %d Patzer, %d Fehler, %d ungenau, %d verpasst\n",
                      c == Color::White ? "  Weiß:   " : "  Schwarz:", s.accuracy, s.avgCpLoss,
                      s.counts[static_cast<size_t>(MoveQuality::Blunder)],
                      s.counts[static_cast<size_t>(MoveQuality::Mistake)],
                      s.counts[static_cast<size_t>(MoveQuality::Inaccuracy)],
                      s.counts[static_cast<size_t>(MoveQuality::Miss)]);
        out += buf;
    }
    return out;
}

}  // namespace chess
