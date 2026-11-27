#include "core/openings.hpp"

#include <sstream>

namespace chess {

namespace {

struct Raw {
    char side;  // 'W' oder 'B': wer diese Eroeffnung im Training spielt
    const char* name;
    const char* moves;
};

// Reihenfolge egal; gesucht wird die laengste passende Zugfolge.
constexpr Raw kOpenings[] = {
    // --- Erste Zuege -------------------------------------------------------------
    {'W', "Königsbauernspiel", "e4"},
    {'W', "Damenbauernspiel", "d4"},
    {'W', "Englische Eröffnung", "c4"},
    {'W', "Réti-Eröffnung", "Nf3"},
    {'W', "Bird-Eröffnung", "f4"},
    {'W', "Nimzowitsch-Larsen-Angriff", "b3"},
    {'W', "Benkö-Eröffnung", "g3"},
    {'W', "Polnische Eröffnung", "b4"},
    {'W', "Grob-Angriff", "g4"},

    // --- Offene Spiele (1. e4 e5) ------------------------------------------------
    {'W', "Offene Spiele", "e4 e5"},
    {'W', "Königsspringerspiel", "e4 e5 Nf3"},
    {'W', "Italienische Partie", "e4 e5 Nf3 Nc6 Bc4"},
    {'W', "Italienisch, Giuoco Piano", "e4 e5 Nf3 Nc6 Bc4 Bc5"},
    {'W', "Italienisch, Hauptvariante mit c3", "e4 e5 Nf3 Nc6 Bc4 Bc5 c3"},
    {'W', "Italienisch, Giuoco Pianissimo", "e4 e5 Nf3 Nc6 Bc4 Bc5 d3"},
    {'W', "Italienisch, Evans-Gambit", "e4 e5 Nf3 Nc6 Bc4 Bc5 b4"},
    {'B', "Ungarische Verteidigung", "e4 e5 Nf3 Nc6 Bc4 Be7"},
    {'B', "Zweispringerspiel im Nachzuge", "e4 e5 Nf3 Nc6 Bc4 Nf6"},
    {'W', "Zweispringerspiel, Angriff mit Sg5", "e4 e5 Nf3 Nc6 Bc4 Nf6 Ng5"},
    {'B', "Zweispringerspiel, Traxler-Gegenangriff", "e4 e5 Nf3 Nc6 Bc4 Nf6 Ng5 Bc5"},
    {'B', "Zweispringerspiel, Hauptvariante", "e4 e5 Nf3 Nc6 Bc4 Nf6 Ng5 d5 exd5 Na5"},
    {'W', "Zweispringerspiel, Fegatello-Angriff", "e4 e5 Nf3 Nc6 Bc4 Nf6 Ng5 d5 exd5 Nxd5 Nxf7"},
    {'W', "Zweispringerspiel, Moderner Angriff mit d4", "e4 e5 Nf3 Nc6 Bc4 Nf6 d4"},
    {'W', "Spanische Partie", "e4 e5 Nf3 Nc6 Bb5"},
    {'B', "Spanisch, Morphy-Verteidigung", "e4 e5 Nf3 Nc6 Bb5 a6"},
    {'W', "Spanisch, Abtauschvariante", "e4 e5 Nf3 Nc6 Bb5 a6 Bxc6"},
    {'W', "Spanisch, Geschlossene Variante", "e4 e5 Nf3 Nc6 Bb5 a6 Ba4 Nf6 O-O Be7"},
    {'B', "Spanisch, Offene Variante", "e4 e5 Nf3 Nc6 Bb5 a6 Ba4 Nf6 O-O Nxe4"},
    {'B', "Spanisch, Marshall-Angriff", "e4 e5 Nf3 Nc6 Bb5 a6 Ba4 Nf6 O-O Be7 Re1 b5 Bb3 O-O c3 d5"},
    {'B', "Spanisch, Berliner Verteidigung", "e4 e5 Nf3 Nc6 Bb5 Nf6"},
    {'B', "Spanisch, Berliner Mauer", "e4 e5 Nf3 Nc6 Bb5 Nf6 O-O Nxe4 d4 Nd6 Bxc6 dxc6 dxe5 Nf5 Qxd8 Kxd8"},
    {'B', "Spanisch, Schliemann-Gambit", "e4 e5 Nf3 Nc6 Bb5 f5"},
    {'B', "Spanisch, Klassische Verteidigung", "e4 e5 Nf3 Nc6 Bb5 Bc5"},
    {'W', "Schottische Partie", "e4 e5 Nf3 Nc6 d4"},
    {'W', "Schottisch, Hauptvariante", "e4 e5 Nf3 Nc6 d4 exd4 Nxd4"},
    {'W', "Schottisches Gambit", "e4 e5 Nf3 Nc6 d4 exd4 Bc4"},
    {'W', "Dreispringerspiel", "e4 e5 Nf3 Nc6 Nc3"},
    {'W', "Vierspringerspiel", "e4 e5 Nf3 Nc6 Nc3 Nf6"},
    {'W', "Vierspringerspiel, Spanische Variante", "e4 e5 Nf3 Nc6 Nc3 Nf6 Bb5"},
    {'W', "Ponziani-Eröffnung", "e4 e5 Nf3 Nc6 c3"},
    {'B', "Philidor-Verteidigung", "e4 e5 Nf3 d6"},
    {'B', "Russische Verteidigung", "e4 e5 Nf3 Nf6"},
    {'B', "Lettisches Gambit", "e4 e5 Nf3 f5"},
    {'B', "Elefantengambit", "e4 e5 Nf3 d5"},
    {'W', "Königsgambit", "e4 e5 f4"},
    {'W', "Angenommenes Königsgambit", "e4 e5 f4 exf4"},
    {'B', "Abgelehntes Königsgambit", "e4 e5 f4 Bc5"},
    {'B', "Falkbeer-Gegengambit", "e4 e5 f4 d5"},
    {'W', "Wiener Partie", "e4 e5 Nc3"},
    {'W', "Läuferspiel", "e4 e5 Bc4"},
    {'W', "Mittelgambit", "e4 e5 d4 exd4"},
    {'W', "Nordisches Gambit", "e4 e5 d4 exd4 c3"},

    // --- Halboffene Spiele ---------------------------------------------------------
    {'B', "Sizilianische Verteidigung", "e4 c5"},
    {'W', "Sizilianisch, Offene Variante", "e4 c5 Nf3 d6 d4 cxd4 Nxd4"},
    {'B', "Sizilianisch, Najdorf-Variante", "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 a6"},
    {'B', "Sizilianisch, Drachenvariante", "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 g6"},
    {'B', "Sizilianisch, Klassische Variante", "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 Nc6"},
    {'B', "Sizilianisch, Scheveninger Variante", "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 e6"},
    {'B', "Sizilianisch, Paulsen-Variante", "e4 c5 Nf3 e6 d4 cxd4 Nxd4 a6"},
    {'B', "Sizilianisch, Taimanow-Variante", "e4 c5 Nf3 e6 d4 cxd4 Nxd4 Nc6"},
    {'B', "Sizilianisch, Sweschnikow-Variante", "e4 c5 Nf3 Nc6 d4 cxd4 Nxd4 Nf6 Nc3 e5"},
    {'B', "Sizilianisch, Beschleunigter Drache", "e4 c5 Nf3 Nc6 d4 cxd4 Nxd4 g6"},
    {'W', "Sizilianisch, Rossolimo-Variante", "e4 c5 Nf3 Nc6 Bb5"},
    {'W', "Sizilianisch, Moskauer Variante", "e4 c5 Nf3 d6 Bb5"},
    {'W', "Sizilianisch, Alapin-Variante", "e4 c5 c3"},
    {'W', "Sizilianisch, Geschlossene Variante", "e4 c5 Nc3"},
    {'W', "Sizilianisch, Grand-Prix-Angriff", "e4 c5 f4"},
    {'W', "Sizilianisch, Grand-Prix-Angriff", "e4 c5 Nc3 Nc6 f4"},
    {'W', "Sizilianisch, Smith-Morra-Gambit", "e4 c5 d4 cxd4 c3"},
    {'B', "Französische Verteidigung", "e4 e6"},
    {'B', "Französisch, Hauptlinie", "e4 e6 d4 d5"},
    {'W', "Französisch, Abtauschvariante", "e4 e6 d4 d5 exd5"},
    {'W', "Französisch, Vorstoßvariante", "e4 e6 d4 d5 e5"},
    {'W', "Französisch, Tarrasch-Variante", "e4 e6 d4 d5 Nd2"},
    {'B', "Französisch, Winawer-Variante", "e4 e6 d4 d5 Nc3 Bb4"},
    {'B', "Französisch, Klassische Variante", "e4 e6 d4 d5 Nc3 Nf6"},
    {'B', "Französisch, Rubinstein-Variante", "e4 e6 d4 d5 Nc3 dxe4"},
    {'B', "Caro-Kann-Verteidigung", "e4 c6"},
    {'W', "Caro-Kann, Vorstoßvariante", "e4 c6 d4 d5 e5"},
    {'W', "Caro-Kann, Abtauschvariante", "e4 c6 d4 d5 exd5 cxd5"},
    {'W', "Caro-Kann, Panow-Angriff", "e4 c6 d4 d5 exd5 cxd5 c4"},
    {'B', "Caro-Kann, Klassische Variante", "e4 c6 d4 d5 Nc3 dxe4 Nxe4 Bf5"},
    {'W', "Caro-Kann, Zweispringer-Variante", "e4 c6 Nc3 d5 Nf3"},
    {'B', "Skandinavische Verteidigung", "e4 d5"},
    {'B', "Skandinavisch, Hauptvariante", "e4 d5 exd5 Qxd5 Nc3 Qa5"},
    {'B', "Skandinavisch, Moderne Variante", "e4 d5 exd5 Nf6"},
    {'B', "Pirc-Verteidigung", "e4 d6 d4 Nf6 Nc3 g6"},
    {'B', "Moderne Verteidigung", "e4 g6"},
    {'B', "Aljechin-Verteidigung", "e4 Nf6"},
    {'B', "Owen-Verteidigung", "e4 b6"},
    {'B', "Nimzowitsch-Verteidigung", "e4 Nc6"},

    // --- Geschlossene Spiele (1. d4) ---------------------------------------------
    {'W', "Damengambit", "d4 d5 c4"},
    {'B', "Angenommenes Damengambit", "d4 d5 c4 dxc4"},
    {'B', "Abgelehntes Damengambit", "d4 d5 c4 e6"},
    {'B', "Abgelehntes Damengambit, Orthodoxe Verteidigung", "d4 d5 c4 e6 Nc3 Nf6 Bg5 Be7"},
    {'B', "Abgelehntes Damengambit, Tarrasch-Verteidigung", "d4 d5 c4 e6 Nc3 c5"},
    {'W', "Abgelehntes Damengambit, Abtauschvariante", "d4 d5 c4 e6 Nc3 Nf6 cxd5"},
    {'B', "Slawische Verteidigung", "d4 d5 c4 c6"},
    {'B', "Halbslawische Verteidigung", "d4 d5 c4 c6 Nf3 Nf6 Nc3 e6"},
    {'B', "Albins Gegengambit", "d4 d5 c4 e5"},
    {'B', "Tschigorin-Verteidigung", "d4 d5 c4 Nc6"},
    {'W', "Londoner System", "d4 d5 Bf4"},
    {'W', "Londoner System", "d4 Nf6 Bf4"},
    {'W', "Londoner System", "d4 d5 Nf3 Nf6 Bf4"},
    {'W', "Londoner System", "d4 Nf6 Nf3 d5 Bf4"},
    {'W', "Colle-System", "d4 d5 Nf3 Nf6 e3"},
    {'W', "Trompowsky-Angriff", "d4 Nf6 Bg5"},
    {'B', "Indische Verteidigung", "d4 Nf6"},
    {'B', "Königsindischer Aufbau", "d4 Nf6 c4 g6"},
    {'B', "Königsindische Verteidigung", "d4 Nf6 c4 g6 Nc3 Bg7 e4 d6"},
    {'B', "Grünfeld-Indische Verteidigung", "d4 Nf6 c4 g6 Nc3 d5"},
    {'B', "Nimzoindische Verteidigung", "d4 Nf6 c4 e6 Nc3 Bb4"},
    {'B', "Damenindische Verteidigung", "d4 Nf6 c4 e6 Nf3 b6"},
    {'B', "Bogoljubow-Indische Verteidigung", "d4 Nf6 c4 e6 Nf3 Bb4"},
    {'W', "Katalanische Eröffnung", "d4 Nf6 c4 e6 g3"},
    {'B', "Benoni-Verteidigung", "d4 Nf6 c4 c5 d5"},
    {'B', "Moderne Benoni", "d4 Nf6 c4 c5 d5 e6"},
    {'B', "Wolga-Gambit", "d4 Nf6 c4 c5 d5 b5"},
    {'B', "Budapester Gambit", "d4 Nf6 c4 e5"},
    {'B', "Holländische Verteidigung", "d4 f5"},
    {'B', "Holländisch, Stonewall-Variante", "d4 f5 c4 Nf6 g3 e6 Bg2 d5"},
    {'W', "Staunton-Gambit", "d4 f5 e4"},
    {'B', "Englund-Gambit", "d4 e5"},

    // --- Fluegelspiele -------------------------------------------------------------
    {'B', "Englisch, Symmetrische Variante", "c4 c5"},
    {'B', "Englisch, Umgekehrtes Sizilianisch", "c4 e5"},
    {'B', "Englisch, Anglo-Indische Verteidigung", "c4 Nf6"},
    {'W', "Réti-Eröffnung, Hauptvariante", "Nf3 d5 c4"},
    {'W', "Königsindischer Angriff", "Nf3 Nf6 g3"},
    {'W', "Königsindischer Angriff", "e4 e6 d3"},
    {'W', "Königsindischer Angriff", "Nf3 d5 g3"},
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
        for (const Raw& r : kOpenings) v.push_back(Opening{r.name, split(r.moves), r.side == 'B'});
        return v;
    }();
    return list;
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
