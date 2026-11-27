#pragma once

// Eroeffnungsnamen: bekannte Zugfolgen mit deutschen Namen. Erkennt die laengste passende Folge
// und liefert die Zuege, die man in der Theorie bleiben koennte (fuer das Eroeffnungs-Training).

#include <string>
#include <vector>

namespace chess {

struct Opening {
    std::string name;
    std::vector<std::string> moves;  // SAN ohne Schach-/Bewertungszeichen
    bool forBlack = false;           // wer die Eroeffnung "waehlt" (fuer das Training)
};

const std::vector<Opening>& openings();

struct OpeningMatch {
    const Opening* opening = nullptr;  // nullptr: keine bekannte Eroeffnung
    int plies = 0;                     // so viele Halbzuege stimmen mit der Theorie ueberein
};

// Laengste bekannte Eroeffnung, die mit der Partie beginnt. `san` darf "+", "#", "!" enthalten.
OpeningMatch identifyOpening(const std::vector<std::string>& san);

// SAN ohne "+", "#", "!", "?" (zum Vergleichen)
std::string plainSan(const std::string& san);

}  // namespace chess
