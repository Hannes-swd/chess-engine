#include "core/openings.hpp"

#include <sstream>

namespace chess {

namespace {

struct Raw {
    char side;  // 'W' oder 'B': wer diese Eroeffnung im Training spielt
    const char* name;
    const char* moves;
    const char* nameEn;
};

// Reihenfolge egal; gesucht wird die laengste passende Zugfolge.
constexpr Raw kOpenings[] = {
    // --- Erste Zuege -------------------------------------------------------------
    {'W', "Königsbauernspiel", "e4", "King's Pawn Opening"},
    {'W', "Damenbauernspiel", "d4", "Queen's Pawn Opening"},
    {'W', "Englische Eröffnung", "c4", "English Opening"},
    {'W', "Réti-Eröffnung", "Nf3", "Réti Opening"},
    {'W', "Bird-Eröffnung", "f4", "Bird's Opening"},
    {'W', "Nimzowitsch-Larsen-Angriff", "b3", "Nimzo-Larsen Attack"},
    {'W', "Benkö-Eröffnung", "g3", "King's Fianchetto Opening"},
    {'W', "Polnische Eröffnung", "b4", "Polish Opening"},
    {'W', "Grob-Angriff", "g4", "Grob Attack"},

    // --- Offene Spiele (1. e4 e5) ------------------------------------------------
    {'W', "Offene Spiele", "e4 e5", "Open Game"},
    {'W', "Königsspringerspiel", "e4 e5 Nf3", "King's Knight Opening"},
    {'W', "Italienische Partie", "e4 e5 Nf3 Nc6 Bc4", "Italian Game"},
    {'W', "Italienisch, Giuoco Piano", "e4 e5 Nf3 Nc6 Bc4 Bc5", "Italian Game, Giuoco Piano"},
    {'W', "Italienisch, Hauptvariante mit c3", "e4 e5 Nf3 Nc6 Bc4 Bc5 c3", "Italian Game, Main Line with c3"},
    {'W', "Italienisch, Giuoco Pianissimo", "e4 e5 Nf3 Nc6 Bc4 Bc5 d3", "Italian Game, Giuoco Pianissimo"},
    {'W', "Italienisch, Evans-Gambit", "e4 e5 Nf3 Nc6 Bc4 Bc5 b4", "Evans Gambit"},
    {'B', "Ungarische Verteidigung", "e4 e5 Nf3 Nc6 Bc4 Be7", "Hungarian Defense"},
    {'B', "Zweispringerspiel im Nachzuge", "e4 e5 Nf3 Nc6 Bc4 Nf6", "Two Knights Defense"},
    {'W', "Zweispringerspiel, Angriff mit Sg5", "e4 e5 Nf3 Nc6 Bc4 Nf6 Ng5", "Two Knights Defense, Knight Attack"},
    {'B', "Zweispringerspiel, Traxler-Gegenangriff", "e4 e5 Nf3 Nc6 Bc4 Nf6 Ng5 Bc5", "Two Knights Defense, Traxler Counterattack"},
    {'B', "Zweispringerspiel, Hauptvariante", "e4 e5 Nf3 Nc6 Bc4 Nf6 Ng5 d5 exd5 Na5", "Two Knights Defense, Main Line"},
    {'W', "Zweispringerspiel, Fegatello-Angriff", "e4 e5 Nf3 Nc6 Bc4 Nf6 Ng5 d5 exd5 Nxd5 Nxf7", "Fried Liver Attack"},
    {'W', "Zweispringerspiel, Moderner Angriff mit d4", "e4 e5 Nf3 Nc6 Bc4 Nf6 d4", "Two Knights Defense, Modern Attack"},
    {'W', "Spanische Partie", "e4 e5 Nf3 Nc6 Bb5", "Ruy Lopez"},
    {'B', "Spanisch, Morphy-Verteidigung", "e4 e5 Nf3 Nc6 Bb5 a6", "Ruy Lopez, Morphy Defense"},
    {'W', "Spanisch, Abtauschvariante", "e4 e5 Nf3 Nc6 Bb5 a6 Bxc6", "Ruy Lopez, Exchange Variation"},
    {'W', "Spanisch, Geschlossene Variante", "e4 e5 Nf3 Nc6 Bb5 a6 Ba4 Nf6 O-O Be7", "Ruy Lopez, Closed"},
    {'B', "Spanisch, Offene Variante", "e4 e5 Nf3 Nc6 Bb5 a6 Ba4 Nf6 O-O Nxe4", "Ruy Lopez, Open"},
    {'B', "Spanisch, Marshall-Angriff", "e4 e5 Nf3 Nc6 Bb5 a6 Ba4 Nf6 O-O Be7 Re1 b5 Bb3 O-O c3 d5", "Ruy Lopez, Marshall Attack"},
    {'B', "Spanisch, Berliner Verteidigung", "e4 e5 Nf3 Nc6 Bb5 Nf6", "Ruy Lopez, Berlin Defense"},
    {'B', "Spanisch, Berliner Mauer", "e4 e5 Nf3 Nc6 Bb5 Nf6 O-O Nxe4 d4 Nd6 Bxc6 dxc6 dxe5 Nf5 Qxd8 Kxd8", "Ruy Lopez, Berlin Wall"},
    {'B', "Spanisch, Schliemann-Gambit", "e4 e5 Nf3 Nc6 Bb5 f5", "Ruy Lopez, Schliemann Gambit"},
    {'B', "Spanisch, Klassische Verteidigung", "e4 e5 Nf3 Nc6 Bb5 Bc5", "Ruy Lopez, Classical Defense"},
    {'W', "Schottische Partie", "e4 e5 Nf3 Nc6 d4", "Scotch Game"},
    {'W', "Schottisch, Hauptvariante", "e4 e5 Nf3 Nc6 d4 exd4 Nxd4", "Scotch Game, Main Line"},
    {'W', "Schottisches Gambit", "e4 e5 Nf3 Nc6 d4 exd4 Bc4", "Scotch Gambit"},
    {'W', "Dreispringerspiel", "e4 e5 Nf3 Nc6 Nc3", "Three Knights Game"},
    {'W', "Vierspringerspiel", "e4 e5 Nf3 Nc6 Nc3 Nf6", "Four Knights Game"},
    {'W', "Vierspringerspiel, Spanische Variante", "e4 e5 Nf3 Nc6 Nc3 Nf6 Bb5", "Four Knights Game, Spanish Variation"},
    {'W', "Ponziani-Eröffnung", "e4 e5 Nf3 Nc6 c3", "Ponziani Opening"},
    {'B', "Philidor-Verteidigung", "e4 e5 Nf3 d6", "Philidor Defense"},
    {'B', "Russische Verteidigung", "e4 e5 Nf3 Nf6", "Petrov's Defense"},
    {'B', "Lettisches Gambit", "e4 e5 Nf3 f5", "Latvian Gambit"},
    {'B', "Elefantengambit", "e4 e5 Nf3 d5", "Elephant Gambit"},
    {'W', "Königsgambit", "e4 e5 f4", "King's Gambit"},
    {'W', "Angenommenes Königsgambit", "e4 e5 f4 exf4", "King's Gambit Accepted"},
    {'B', "Abgelehntes Königsgambit", "e4 e5 f4 Bc5", "King's Gambit Declined"},
    {'B', "Falkbeer-Gegengambit", "e4 e5 f4 d5", "Falkbeer Countergambit"},
    {'W', "Wiener Partie", "e4 e5 Nc3", "Vienna Game"},
    {'W', "Läuferspiel", "e4 e5 Bc4", "Bishop's Opening"},
    {'W', "Mittelgambit", "e4 e5 d4 exd4", "Center Game"},
    {'W', "Nordisches Gambit", "e4 e5 d4 exd4 c3", "Danish Gambit"},

    // --- Halboffene Spiele ---------------------------------------------------------
    {'B', "Sizilianische Verteidigung", "e4 c5", "Sicilian Defense"},
    {'W', "Sizilianisch, Offene Variante", "e4 c5 Nf3 d6 d4 cxd4 Nxd4", "Sicilian Defense, Open"},
    {'B', "Sizilianisch, Najdorf-Variante", "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 a6", "Sicilian Defense, Najdorf Variation"},
    {'B', "Sizilianisch, Drachenvariante", "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 g6", "Sicilian Defense, Dragon Variation"},
    {'B', "Sizilianisch, Klassische Variante", "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 Nc6", "Sicilian Defense, Classical Variation"},
    {'B', "Sizilianisch, Scheveninger Variante", "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 e6", "Sicilian Defense, Scheveningen Variation"},
    {'B', "Sizilianisch, Paulsen-Variante", "e4 c5 Nf3 e6 d4 cxd4 Nxd4 a6", "Sicilian Defense, Kan Variation"},
    {'B', "Sizilianisch, Taimanow-Variante", "e4 c5 Nf3 e6 d4 cxd4 Nxd4 Nc6", "Sicilian Defense, Taimanov Variation"},
    {'B', "Sizilianisch, Sweschnikow-Variante", "e4 c5 Nf3 Nc6 d4 cxd4 Nxd4 Nf6 Nc3 e5", "Sicilian Defense, Sveshnikov Variation"},
    {'B', "Sizilianisch, Beschleunigter Drache", "e4 c5 Nf3 Nc6 d4 cxd4 Nxd4 g6", "Sicilian Defense, Accelerated Dragon"},
    {'W', "Sizilianisch, Rossolimo-Variante", "e4 c5 Nf3 Nc6 Bb5", "Sicilian Defense, Rossolimo Variation"},
    {'W', "Sizilianisch, Moskauer Variante", "e4 c5 Nf3 d6 Bb5", "Sicilian Defense, Moscow Variation"},
    {'W', "Sizilianisch, Alapin-Variante", "e4 c5 c3", "Sicilian Defense, Alapin Variation"},
    {'W', "Sizilianisch, Geschlossene Variante", "e4 c5 Nc3", "Sicilian Defense, Closed"},
    {'W', "Sizilianisch, Grand-Prix-Angriff", "e4 c5 f4", "Sicilian Defense, Grand Prix Attack"},
    {'W', "Sizilianisch, Grand-Prix-Angriff", "e4 c5 Nc3 Nc6 f4", "Sicilian Defense, Grand Prix Attack"},
    {'W', "Sizilianisch, Smith-Morra-Gambit", "e4 c5 d4 cxd4 c3", "Sicilian Defense, Smith-Morra Gambit"},
    {'B', "Französische Verteidigung", "e4 e6", "French Defense"},
    {'B', "Französisch, Hauptlinie", "e4 e6 d4 d5", "French Defense, Main Line"},
    {'W', "Französisch, Abtauschvariante", "e4 e6 d4 d5 exd5", "French Defense, Exchange Variation"},
    {'W', "Französisch, Vorstoßvariante", "e4 e6 d4 d5 e5", "French Defense, Advance Variation"},
    {'W', "Französisch, Tarrasch-Variante", "e4 e6 d4 d5 Nd2", "French Defense, Tarrasch Variation"},
    {'B', "Französisch, Winawer-Variante", "e4 e6 d4 d5 Nc3 Bb4", "French Defense, Winawer Variation"},
    {'B', "Französisch, Klassische Variante", "e4 e6 d4 d5 Nc3 Nf6", "French Defense, Classical Variation"},
    {'B', "Französisch, Rubinstein-Variante", "e4 e6 d4 d5 Nc3 dxe4", "French Defense, Rubinstein Variation"},
    {'B', "Caro-Kann-Verteidigung", "e4 c6", "Caro-Kann Defense"},
    {'W', "Caro-Kann, Vorstoßvariante", "e4 c6 d4 d5 e5", "Caro-Kann Defense, Advance Variation"},
    {'W', "Caro-Kann, Abtauschvariante", "e4 c6 d4 d5 exd5 cxd5", "Caro-Kann Defense, Exchange Variation"},
    {'W', "Caro-Kann, Panow-Angriff", "e4 c6 d4 d5 exd5 cxd5 c4", "Caro-Kann Defense, Panov Attack"},
    {'B', "Caro-Kann, Klassische Variante", "e4 c6 d4 d5 Nc3 dxe4 Nxe4 Bf5", "Caro-Kann Defense, Classical Variation"},
    {'W', "Caro-Kann, Zweispringer-Variante", "e4 c6 Nc3 d5 Nf3", "Caro-Kann Defense, Two Knights Attack"},
    {'B', "Skandinavische Verteidigung", "e4 d5", "Scandinavian Defense"},
    {'B', "Skandinavisch, Hauptvariante", "e4 d5 exd5 Qxd5 Nc3 Qa5", "Scandinavian Defense, Main Line"},
    {'B', "Skandinavisch, Moderne Variante", "e4 d5 exd5 Nf6", "Scandinavian Defense, Modern Variation"},
    {'B', "Pirc-Verteidigung", "e4 d6 d4 Nf6 Nc3 g6", "Pirc Defense"},
    {'B', "Moderne Verteidigung", "e4 g6", "Modern Defense"},
    {'B', "Aljechin-Verteidigung", "e4 Nf6", "Alekhine's Defense"},
    {'B', "Owen-Verteidigung", "e4 b6", "Owen's Defense"},
    {'B', "Nimzowitsch-Verteidigung", "e4 Nc6", "Nimzowitsch Defense"},

    // --- Geschlossene Spiele (1. d4) ---------------------------------------------
    {'W', "Damengambit", "d4 d5 c4", "Queen's Gambit"},
    {'B', "Angenommenes Damengambit", "d4 d5 c4 dxc4", "Queen's Gambit Accepted"},
    {'B', "Abgelehntes Damengambit", "d4 d5 c4 e6", "Queen's Gambit Declined"},
    {'B', "Abgelehntes Damengambit, Orthodoxe Verteidigung", "d4 d5 c4 e6 Nc3 Nf6 Bg5 Be7", "Queen's Gambit Declined, Orthodox Defense"},
    {'B', "Abgelehntes Damengambit, Tarrasch-Verteidigung", "d4 d5 c4 e6 Nc3 c5", "Queen's Gambit Declined, Tarrasch Defense"},
    {'W', "Abgelehntes Damengambit, Abtauschvariante", "d4 d5 c4 e6 Nc3 Nf6 cxd5", "Queen's Gambit Declined, Exchange Variation"},
    {'B', "Slawische Verteidigung", "d4 d5 c4 c6", "Slav Defense"},
    {'B', "Halbslawische Verteidigung", "d4 d5 c4 c6 Nf3 Nf6 Nc3 e6", "Semi-Slav Defense"},
    {'B', "Albins Gegengambit", "d4 d5 c4 e5", "Albin Countergambit"},
    {'B', "Tschigorin-Verteidigung", "d4 d5 c4 Nc6", "Chigorin Defense"},
    {'W', "Londoner System", "d4 d5 Bf4", "London System"},
    {'W', "Londoner System", "d4 Nf6 Bf4", "London System"},
    {'W', "Londoner System", "d4 d5 Nf3 Nf6 Bf4", "London System"},
    {'W', "Londoner System", "d4 Nf6 Nf3 d5 Bf4", "London System"},
    {'W', "Colle-System", "d4 d5 Nf3 Nf6 e3", "Colle System"},
    {'W', "Trompowsky-Angriff", "d4 Nf6 Bg5", "Trompowsky Attack"},
    {'B', "Indische Verteidigung", "d4 Nf6", "Indian Defense"},
    {'B', "Königsindischer Aufbau", "d4 Nf6 c4 g6", "King's Indian Setup"},
    {'B', "Königsindische Verteidigung", "d4 Nf6 c4 g6 Nc3 Bg7 e4 d6", "King's Indian Defense"},
    {'B', "Grünfeld-Indische Verteidigung", "d4 Nf6 c4 g6 Nc3 d5", "Grünfeld Defense"},
    {'B', "Nimzoindische Verteidigung", "d4 Nf6 c4 e6 Nc3 Bb4", "Nimzo-Indian Defense"},
    {'B', "Damenindische Verteidigung", "d4 Nf6 c4 e6 Nf3 b6", "Queen's Indian Defense"},
    {'B', "Bogoljubow-Indische Verteidigung", "d4 Nf6 c4 e6 Nf3 Bb4", "Bogo-Indian Defense"},
    {'W', "Katalanische Eröffnung", "d4 Nf6 c4 e6 g3", "Catalan Opening"},
    {'B', "Benoni-Verteidigung", "d4 Nf6 c4 c5 d5", "Benoni Defense"},
    {'B', "Moderne Benoni", "d4 Nf6 c4 c5 d5 e6", "Modern Benoni"},
    {'B', "Wolga-Gambit", "d4 Nf6 c4 c5 d5 b5", "Benko Gambit"},
    {'B', "Budapester Gambit", "d4 Nf6 c4 e5", "Budapest Gambit"},
    {'B', "Holländische Verteidigung", "d4 f5", "Dutch Defense"},
    {'B', "Holländisch, Stonewall-Variante", "d4 f5 c4 Nf6 g3 e6 Bg2 d5", "Dutch Defense, Stonewall Variation"},
    {'W', "Staunton-Gambit", "d4 f5 e4", "Staunton Gambit"},
    {'B', "Englund-Gambit", "d4 e5", "Englund Gambit"},

    // --- Fluegelspiele -------------------------------------------------------------
    {'B', "Englisch, Symmetrische Variante", "c4 c5", "English Opening, Symmetrical Variation"},
    {'B', "Englisch, Umgekehrtes Sizilianisch", "c4 e5", "English Opening, Reversed Sicilian"},
    {'B', "Englisch, Anglo-Indische Verteidigung", "c4 Nf6", "English Opening, Anglo-Indian Defense"},
    {'W', "Réti-Eröffnung, Hauptvariante", "Nf3 d5 c4", "Réti Opening, Main Line"},
    {'W', "Königsindischer Angriff", "Nf3 Nf6 g3", "King's Indian Attack"},
    {'W', "Königsindischer Angriff", "e4 e6 d3", "King's Indian Attack"},
    {'W', "Königsindischer Angriff", "Nf3 d5 g3", "King's Indian Attack"},
};

std::vector<std::string> split(const char* text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string tok;
    while (in >> tok) out.push_back(tok);
    return out;
}

}  // namespace

std::string plainSan(const std::string& san) {
    std::string out;
    for (char c : san)
        if (c != '+' && c != '#' && c != '!' && c != '?') out += c;
    return out;
}

const std::vector<Opening>& openings() {
    static const std::vector<Opening> list = [] {
        std::vector<Opening> v;
        for (const Raw& r : kOpenings) v.push_back(Opening{r.name, split(r.moves), r.side == 'B', r.nameEn});
        return v;
    }();
    return list;
}

std::string localizedOpeningName(const std::string& german, bool english) {
    if (!english) return german;
    for (const Opening& o : openings())
        if (o.name == german) return o.nameEn;
    return german;
}

OpeningMatch identifyOpening(const std::vector<std::string>& san) {
    OpeningMatch best;
    std::vector<std::string> plain;
    plain.reserve(san.size());
    for (const auto& s : san) plain.push_back(plainSan(s));

    for (const Opening& o : openings()) {
        if (o.moves.size() > plain.size()) continue;
        bool match = true;
        for (size_t i = 0; i < o.moves.size() && match; ++i) match = o.moves[i] == plain[i];
        if (match && static_cast<int>(o.moves.size()) > best.plies) {
            best.opening = &o;
            best.plies = static_cast<int>(o.moves.size());
        }
    }
    return best;
}

}  // namespace chess
