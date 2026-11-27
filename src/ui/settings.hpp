#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace chess::ui {

// Alle Hilfen einzeln abschaltbar. Wird in Dokumente\ChessEngine\einstellungen.json gespeichert.
struct AssistSettings {
    // Beim Ziehen
    bool legalMoves = true;     // Punkte fuer moegliche Zuege
    bool dangerTargets = true;  // Zielfelder, die der Gegner angreift, rot
    bool targetFrame = true;    // Rahmen ums Zielfeld unter der Maus

    // Waehrend der Partie
    bool hangingPieces = false;  // eigene angegriffene, ungedeckte Figuren markieren
    bool checkGlow = true;       // roter Schein um den Koenig im Schach
    bool lastMove = true;        // letzten Zug hervorheben
    bool material = true;        // Materialvorteil "+2" neben dem Namen
    bool hintButton = true;      // Tipp-Funktion (H)
    bool undo = true;            // Zuruecknehmen erlaubt
    bool botInfo = true;         // Begruendung und Bewertung des Bots zeigen
    bool annotations = true;     // eigene Pfeile und Kreise per Rechtsklick

    // Analyse
    bool evalBar = true;    // Bewertungsbalken neben dem Brett
    bool bestArrow = true;  // bester Zug als Pfeil
    bool moveMarks = true;  // ?! ? ?? in der Zugliste

    // Darstellung
    bool coordinates = true;
    bool darkTheme = true;
    bool sidebarExpanded = true;  // linke Leiste mit Beschriftung oder nur Icons
    bool autoAnalyse = true;      // Partie nach dem Ende automatisch analysieren
    bool animations = true;       // Figuren gleiten statt zu springen
    bool sounds = true;           // Klang fuer Zug, Schlag, Schach, Zeitnot
    bool premoves = true;         // Zug vormerken, waehrend der Bot denkt
    int boardTheme = 0;           // Index in Theme::boardThemes()
    int language = 0;             // 0 Deutsch, 1 Englisch
    int pieceSet = 0;             // 0 Klassisch, 1 Diagramm, 2 Buchstaben
    std::vector<std::string> plugins;  // aktive Plugins (Ordnernamen)

    static AssistSettings learning();  // alles an
    static AssistSettings standard();  // Voreinstellung
    static AssistSettings none();      // wie im Turnier: keine Hilfen

    // Nur die Hilfen vergleichen (Design/Koordinaten zaehlen nicht zur Schnellwahl)
    bool sameAssists(const AssistSettings& o) const;

    std::string toJson() const;
    bool fromJson(const std::string& text);  // unbekannte/fehlende Felder behalten ihren Wert
    bool load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;
};

}  // namespace chess::ui
