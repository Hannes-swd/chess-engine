# Schach-Lernprogramm in C++ — Plan

## Context

Du willst ein Schachprogramm zum Lernen, das ohne Paywall auskommt und Dinge kann, die chess.com & Co. nicht bieten:
- Gegen KI-Bots spielen, deren Stärke und Spielstil einstellbar sind (aggressiv, defensiv, Elo/Stärke usw.).
- Partien speichern und alle Züge als Text (PGN) exportieren.
- Jeden Zug einer Partie von Stockfish bewerten lassen (Güte je Zug, z. B. "Blunder", "Best").
- Freie Modding-Möglichkeiten: Plugins, die **alles** im Spiel verändern können — Feldgröße, Startpositionen, Figuren, Regeln, Siegbedingungen, UI, und Dinge, an die noch niemand gedacht hat — aber nur innerhalb des Spiels.
- Eigenes, ruhiges Design, das nicht ablenkt.

Entschieden (aus deinen Antworten):
- Projektort: eigener Ordner `chessengine/`.
- Modus: **nur lokal / Hotseat** (kein Server, kein Online-Spiel). Die Architektur bleibt so, dass Online später nachrüstbar ist.
- Plugins: **Lua mit vollem Spiel-API**, sandboxed (kein Dateisystem, Netzwerk, OS-Zugriff).
- UI: **Dear ImGui + SDL2**.

## Kernidee: "Alles ist überschreibbar"

Damit Plugins wirklich alles ändern können, darf es im Kern **keine hart verdrahteten Regeln** geben. Jede Regel läuft über eine Schnittstelle, die ein Plugin ersetzen kann:

- Brett ist ein generisches **W×H-Gitter** (Standard 8×8), nicht fest 8×8.
- Figuren sind **Daten** (Symbol, Zugmuster, Wert). Zugmuster können Lua-Funktionen sein.
- Regeln sind **Hooks**: `on_setup`, `generate_moves`, `validate_move`, `on_move_played`, `check_game_end`, `evaluate_position`, `on_ui_draw`, …
- Startpositionen, Siegbedingungen, Zugreihenfolge, Uhren usw. sind ebenfalls Hooks oder Daten.
- Ein Plugin kann einen Hook komplett ersetzen oder erweitern (Reihenfolge per Priorität).

Standardregeln sind selbst nur ein Plugin ("standard_chess"). Dann ist das Modell für alle gleich, und Plugins sind nicht zweitrangig.

**Grenze, die du wissen solltest:** Stockfish kennt nur Standardschach (plus einige Varianten). Bei Plugins mit geänderten Regeln, Feldgröße oder neuen Figuren kann Stockfish die Züge nicht sinnvoll bewerten. Das Programm zeigt dann "keine Engine-Analyse für diese Variante" an. Optional kann ein Plugin eine eigene Bewertungsfunktion (`evaluate_position`) in Lua liefern, die dann verwendet wird.

## Architektur

```
chessengine/
├─ CMakeLists.txt          (CMake, Abhängigkeiten über vcpkg)
├─ src/
│  ├─ core/        Brett (W×H), Figuren-Definitionen, Zuggenerierung, Regel-Engine,
│  │               GameState, Hook-System (Event-Bus mit Priorität)
│  ├─ plugins/     Lua-Laufzeit (Sandbox), Manifest-Loader (plugin.json), API-Bindings
│  ├─ engine/      UCI-Client für Stockfish (eigener Prozess), Bot-Profile (JSON)
│  ├─ analysis/    Hintergrund-Analyse einer gespeicherten Partie, Zug-Klassifikation
│  ├─ storage/     SQLite (Partien, Züge, Analysen), PGN-Import/Export, Einstellungen
│  ├─ ui/          ImGui-Screens, Theme (ein Farbsystem mit Tokens), Zen-Modus
│  └─ app/         main, Verdrahtung
├─ plugins/        Beispiel-Plugins (Riesenbrett, Sprungfigur, Startposition-Editor)
├─ bots/           bot-profile.json (Stärke, Aggressivität, Risikobereitschaft, Stil)
└─ tests/          perft-Tests, Regeltests, Sandbox-Tests
```

Trennung: `core` hat **keine** UI- oder Lua-Abhängigkeit. So lassen sich Regeln unabhängig testen.

## Empfohlene Bibliotheken

| Zweck | Bibliothek | Lizenz | Hinweis |
|---|---|---|---|
| UI | Dear ImGui (docking branch) | MIT | Schlank, eigenes Design, ideal für "ruhig & fokussiert" |
| Fenster/Grafik | SDL2 | zlib | Plattform, Eingabe, Rendering-Backend für ImGui |
| Scripting | Lua 5.4 | MIT | Schnell, klein, leicht zu lernen |
| Lua-C++-Bindung | sol2 | MIT | Moderne, typsichere Bindung |
| Datenbank | SQLite3 | Public Domain | Partien, Züge, Analysen in einer Datei |
| JSON | nlohmann/json | MIT | Bot-Profile, Plugin-Manifeste, Export |
| Logging | spdlog | MIT | Optional, aber hilfreich beim Debuggen von Plugins |
| Tests | Catch2 oder GoogleTest | BSL / BSD | Perft-Tests und Regeltests |
| Referenz (optional) | chess-library (Disservin) | MIT | Nur als Vergleich/Testorakel, nicht als Kern |
| Engine | Stockfish (externes Binary) | GPL-3 | Läuft als **eigener Prozess** über UCI. Wird es mitgeliefert, gelten die GPL-Pflichten |
| Build | CMake + vcpkg | — | Installiert alle obigen Pakete reproduzierbar |

Stockfish-Binary musst du selbst von der offiziellen Seite herunterladen; das Programm sucht den Pfad in den Einstellungen.

## Funktionen je Baustein

**Bots (Stockfish + eigene Profile)**
- Stärke über `Skill Level` (0–20) oder `UCI_Elo` (Stockfish-Minimum ca. 1320).
- Stil über Kandidatenauswahl: Stockfish liefert `MultiPV` (Top-N-Züge mit Bewertung). Daraus wird gewichtet gewählt:
  - Aggressiv: Bonus für Schläge, Schach, Vorstöße.
  - Defensiv: Bonus für Figurensicherheit, Königsschutz.
  - Zufall/Risiko: Schwankungsbreite in Centipawns.
- Profile liegen als JSON in `bots/` und sind editierbar oder per Plugin erweiterbar.

**Analyse**
- Nach Partieende oder auf Knopfdruck: jede Position wird mit fester Tiefe oder Zeit bewertet (`go depth N`, Hintergrund-Thread).
- Zug-Klassifikation über Centipawn-Verlust (Schwellen konfigurierbar, z. B. Beste / Gut / Ungenau / Fehler / Patzer).
- Ergebnis wird in SQLite gespeichert und kann mit dem PGN exportiert werden (als Kommentar `{[%eval 0.35]}`).

**Partie & Export**
- Hotseat: zwei Spieler am selben Rechner, Züge per Maus oder Tastatur, Zurücknehmen, FEN-Setup.
- Speichern bei Partieende (oder jederzeit) in SQLite.
- PGN-Export (Zwischenablage und Datei). Alle Züge als Text, kostenlos.

**Plugins**
- Ordner `plugins/<name>/` mit `plugin.json` (Name, Version, Priorität, Berechtigungen) und `main.lua`.
- Lua-Sandbox: `io`, `os`, `package`, `debug`, `loadfile`, `dofile` sind entfernt. Eine Instruktionsgrenze stoppt Endlosschleifen.
- Spiel-API (Auszug): `game:set_size(w,h)`, `game:place(piece, square)`, `game:piece_at(x,y)`, `game:add_piece_type{...}`, `game:set_start_fen(...)`, `ui:add_panel(...)`, `log(...)`.
- Beispiel (Skizze):
  ```lua
  return {
    name = "Riesenbrett mit Drache",
    on_setup = function(game)
      game:set_size(10, 10)
      game:add_piece_type{ id = "drache", symbol = "D", value = 9,
        moves = function(game, x, y) --[[ eigene Zugliste ]] end }
    end,
  }
  ```

**Design**
- Ein Farb- und Schrift-Set als Tokens (Hintergrund, Akzent, Text). Dunkel und hell.
- Zen-Modus: Brett zentriert, Seitenpanel ein-/ausklappbar, keine Pop-ups, dezente Animationen, Tastaturkürzel.
- Eigene Brettfarben und Figurenstil (vorerst Unicode-Figuren oder einfache SVG-Grafiken, später Austausch über Plugins/Theme-Dateien).

## Meilensteine

1. **Kern:** Brett W×H, Zuggenerierung, Schach-Regeln, Hook-System. Tests: **perft** Standardstellung Tiefe 5 = **4 865 609** Knoten, dazu Spezialfälle (Rochade, en passant, Umwandlung, Patt, Matt).
2. **GUI Hotseat:** SDL2 + ImGui, Brett zeichnen, Drag & Drop, Undo, FEN.
3. **Speichern & Export:** SQLite-Speicher, Partieliste, PGN-Export.
4. **Stockfish:** UCI-Client, Bots mit Stil-Profilen, Hintergrund-Analyse, Zug-Klassifikation, PGN mit Eval-Kommentaren.
5. **Plugins:** Lua-Loader, Sandbox, Spiel-API, drei Beispiel-Plugins (Riesenbrett, neue Figur, Startposition).
6. **Design:** Theme, Zen-Modus, Einstellungen, Tastatur.
7. **Optional später:** Online-Server (Asio), wenn gewünscht.

## Kritische Dateien

Es gibt noch keinen Code. Neu anzulegen:
- `CMakeLists.txt`
- `src\core\board.hpp` (Brett, Figuren, Zug-Typen)
- `src\core\hooks.hpp` (Event-Bus)
- `src\plugins\lua_sandbox.cpp`
- `src\engine\uci_client.cpp`
- `src\app\main.cpp`

Wiederverwendung: keine vorhandenen Schach-Funktionen im Projekt. Die leere `main.cpp` im Ordner `game-merge` wird nicht genutzt.

## Verifikation

1. **Perft-Tests** (Core): Standardstellung Tiefe 5 = 4 865 609; Kiwipete (Standard-Testpositionen) gegen bekannte Werte.
2. **Regeltests**: Rochade, en passant, Umwandlung, Schach/Matt/Patt, Wiederholung.
3. **Plugin-Sandbox-Test**: Plugin mit `io.open` muss mit Fehler abbrechen; Endlosschleife wird nach Instruktionslimit gestoppt.
4. **Plugin-Test**: Riesenbrett-Plugin lädt, Brett wird 10×10, Züge funktionieren.
5. **Manuell (GUI)**: Hotseat-Partie spielen, speichern, PGN exportieren, in Textdatei prüfen.
6. **Engine-Test**: Stockfish-Binary hinterlegen, Bot mit Stärke 5 gegen Stärke 15 spielen, Analyse einer gespeicherten Partie starten und Klassifikation prüfen.

Vorab zu prüfen (noch nicht geschehen): ob ein C++-Compiler (MSVC/Visual Studio oder MinGW), CMake und vcpkg auf dem Rechner installiert sind.
