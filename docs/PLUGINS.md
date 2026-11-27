# Plugins schreiben

Plugins ändern die Regeln innerhalb des Spiels: Brettgröße, neue Figuren, Startaufstellung, welche Züge
erlaubt sind, was nach einem Zug passiert und wer gewinnt. Sie sind in **Lua 5.4** geschrieben.

## Aufbau

Ein Plugin ist ein Ordner mit zwei Dateien:

```
Dokumente\ChessEngine\Plugins\
  mein_plugin\
    plugin.json   Name, Beschreibung, Priorität
    main.lua      die Regeln
```

Beim ersten Start kopiert das Programm die mitgelieferten Beispiele einmal in diesen Ordner. Die Liste im
Bereich **Plugins** zeigt genau, was dort liegt: Ordner hineinlegen und „Neu einlesen“ klicken, oder einen
Ordner löschen, dann verschwindet das Plugin. Gelöschte Beispiele werden nicht wieder angelegt (das merkt
sich die Datei `.beispiele`).

### plugin.json

```json
{
  "name": "Mein Plugin",
  "description": "Ein Satz, was es ändert.",
  "author": "Du",
  "version": "1.0",
  "priority": 0
}
```

`priority` legt die Reihenfolge fest, wenn mehrere Plugins aktiv sind: höhere Werte laufen zuerst.

### main.lua

`main.lua` gibt eine Tabelle zurück. Alle Funktionen sind optional:

```lua
return {
  on_load      = function(rules) end,                 -- einmal beim Start der Partie
  on_setup     = function(board) end,                 -- Startstellung nachbearbeiten
  filter_moves = function(board, moves) return moves end,
  on_move      = function(board, move) end,           -- nach jedem Zug
  check_end    = function(board, info) end,           -- eigene Sieg-/Remisbedingung
}
```

Aktiviert werden Plugins im Bereich **Plugins**. Sie gelten ab der nächsten neuen Partie.
Partien mit Plugins werden nicht für die Elo gewertet. Stockfish kennt nur normales Schach; bei
geänderten Regeln spielen und analysieren die Bots mit der eingebauten Engine.

## Koordinaten

Felder werden als `x, y` angegeben, beide ab **1**: `(1, 1)` ist a1, `(5, 4)` ist e4.
Farben sind die Texte `"white"` und `"black"`.

## on_load(rules)

Hier werden Figuren und Startstellung festgelegt.

| Funktion | Wirkung |
|---|---|
| `rules.add_piece{...}` | neue Figur (siehe unten) |
| `rules.edit_piece("knight", {...})` | vorhandene Figur ändern, z. B. `{ value = 350 }` |
| `rules.set_start(fen)` | Startstellung als FEN, auch für andere Brettgrößen (`"10/10/..."`) |
| `rules.set_name(text)` | Name der Variante (oben in der Zugliste) |
| `rules.standard_end_rules(false)` | Matt, Patt, 50 Züge und Wiederholung abschalten |
| `rules.piece_ids()` | Liste aller Figuren-ids |

### Figuren

```lua
rules.add_piece{
  id = "drache",            -- eindeutig
  name = "Drache",
  symbol = "D",             -- ein Buchstabe, eindeutig (für FEN und Notation)
  value = 850,              -- Wert in Hundertstel-Bauern (für Engine und Materialanzeige)
  moves_like = { "rook", "knight" },  -- Zugmuster vorhandener Figuren übernehmen
  promotion = true,         -- Bauern dürfen sich in diese Figur umwandeln
}
```

Weitere Felder: `royal = true` (Schach und Matt beziehen sich auf diese Figur), `pawn_like = true`
(Doppelschritt, en passant, Umwandlung), `castles = true`.

Eigene Zugmuster als Schritte:

```lua
steps = {
  { 1, 2 },                       -- ein Sprung (dx, dy), für Schwarz automatisch gespiegelt
  { 0, 1, slide = true },         -- gleitet bis zum nächsten Hindernis
  { 1, 1, only = "capture" },     -- nur zum Schlagen ("move": nur ohne Schlagen)
}
```

Oder eine Lua-Funktion für alles, was sich nicht als Schritt beschreiben lässt. Sie gibt die Zielfelder
zurück; Felder mit eigenen Figuren und Züge, die den eigenen König im Schach lassen, sortiert das
Programm selbst aus:

```lua
moves = function(board, x, y)
  return { { x + 2, y }, { x - 2, y } }
end
```

Standard-ids: `pawn`, `knight`, `bishop`, `rook`, `queen`, `king`.

## Das Brett (`board`)

| Methode | Ergebnis |
|---|---|
| `board:width()`, `board:height()` | Brettgröße |
| `board:inside(x, y)` | liegt das Feld auf dem Brett? |
| `board:get(x, y)` | `id, color` der Figur oder `nil` |
| `board:pieces([color])` | Liste `{ id, color, x, y }` |
| `board:side()` | wer am Zug ist |
| `board:in_check(color)` | steht diese Farbe im Schach? |
| `board:attacked(x, y, color)` | greift `color` das Feld an? |
| `board:fen()` | Stellung als FEN |
| `board:set(x, y, id, color)` | Figur setzen ¹ |
| `board:clear(x, y)` | Feld leeren ¹ |
| `board:resize(w, h)` | Brettgröße ändern, leert das Brett ¹ |
| `board:set_side(color)` | wer am Zug ist ¹ |

¹ Nur in `on_setup` und `on_move`. In `filter_moves`, `check_end` und Zugfunktionen darf das Brett nur
gelesen werden.

## Züge (`move`)

Ein Zug ist eine Tabelle mit `from_x`, `from_y`, `to_x`, `to_y`, `capture` (true/false) und optional
`promotion` (Figuren-id).

`filter_moves(board, moves)` bekommt alle erlaubten Züge und gibt die Liste zurück, die gelten soll.
Einträge entfernen verbietet Züge; neue Tabellen fügen Züge hinzu.

```lua
filter_moves = function(board, moves)
  local captures = {}
  for _, m in ipairs(moves) do
    if m.capture then captures[#captures + 1] = m end
  end
  if #captures > 0 then return captures end   -- Schlagzwang
  return moves
end
```

## Partieende (`check_end`)

`check_end(board, info)` wird nach jedem Zug gefragt. `info.ply` ist die Zahl der gespielten Halbzüge,
`info.last_move` und `info.last_san` beschreiben den letzten Zug. Rückgabe: `"white"`, `"black"` oder
`"draw"` und ein Grund – oder nichts, dann geht die Partie weiter.

```lua
check_end = function(board, info)
  local id, color = board:get(5, 4)
  if id == "king" then return color, "König auf e4" end
end
```

## Grenzen

Plugins laufen abgeschottet:

- kein Zugriff auf Dateien, Programme, Netzwerk oder Betriebssystem (`io`, `os`, `require`, `load` fehlen),
- höchstens 64 MB Speicher und eine Rechenzeit-Grenze pro Aufruf (Endlosschleifen brechen ab),
- Fehler stoppen nur den jeweiligen Aufruf; sie erscheinen im Bereich **Plugins** unter
  „Laufende Partie“, ebenso Ausgaben von `print(...)`.

Verfügbar sind `string`, `table`, `math` und `utf8` aus der Lua-Standardbibliothek.

## Beispiele

| Ordner | zeigt |
|---|---|
| `riesenbrett` | 10×10-Brett, zwei neue Figuren |
| `amazone` | Figur aus zwei Zugmustern |
| `grashuepfer` | Figur mit eigener Zugfunktion in Lua |
| `zufallsaufstellung` | zufällige Startstellung bei jeder Partie |
| `zufallsmittelspiel` | Start mitten in der Partie: eine von 93 ausgeglichenen Stellungen aus bekannten Eröffnungen |
| `koenigshuegel` | zusätzliche Siegbedingung |
| `schlagzwang` | Züge filtern |
| `atomschach` | Brett nach jedem Zug verändern |
