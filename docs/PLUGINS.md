# Plugins schreiben

Plugins ändern die Regeln innerhalb des Spiels: Brettgröße, neue Figuren, Startaufstellung, welche Züge
erlaubt sind, was nach einem Zug passiert und wer gewinnt. Sie sind in **Lua 5.4** geschrieben.

## Aufbau

Ein Plugin ist ein Ordner mit zwei Dateien:

```
Dokumente\ChessEngine\Plugins\
  mein_plugin\
    plugin.json   Name, Beschreibung, Farbe
    main.lua      die Regeln
    icon.png      optional: quadratisches Icon (z. B. 256 × 256), erscheint in der Liste
    vorschau.png  optional: großes Bild für die Detailseite
```

Ohne `vorschau.png` zeigt die Detailseite die echte Startstellung des Plugins als Mini-Brett. Ohne
`icon.png` gibt es eine Farbfläche mit dem Anfangsbuchstaben. PNG, JPG, BMP und GIF werden gelesen;
andere Dateinamen lassen sich in plugin.json mit `"icon"` und `"preview"` angeben.

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
  "color": "#3F6E6A"
}
```

Für Englisch kommen optional `"name_en"` und `"description_en"` dazu; ohne sie zeigt die englische
Oberfläche den deutschen Text.

`color` ist die Akzentfarbe des Plugins (Rahmen der Vorschau, Haken auf der Detailseite, Icon-Ersatz).

Unter **„Was es ändert“** listet die Detailseite selbstständig auf, was das Plugin tatsächlich tut:
Brettgröße, neue Figuren, eigene oder zufällige Startstellung, Zugfilter, Siegbedingungen. Dafür wird das
Plugin einmal probehalber geladen.

Welche Plugins aktiv sind und in welcher Reihenfolge, bestimmst du im Bereich **Plugins**: links
„Verfügbar“, rechts „Aktiv“. Ein Plugin wird per Ziehen (oder Doppelklick) aktiviert; in „Aktiv“ sortierst
du es per Ziehen. **Oben steht das wichtigste**: Wollen zwei Plugins dasselbe festlegen (Startstellung, Name,
eine Figur), gewinnt das obere; seine Regel-Hooks laufen zuerst, und bei `check_end` zählt das erste Ergebnis.
`priority` in plugin.json wird dafür nicht mehr gebraucht.

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

Eigene Bilder für die Figur (PNG/JPG im Plugin-Ordner, am besten quadratisch mit transparentem Rand):

```lua
image_white = "figuren/drache_weiss.png",
image_black = "figuren/drache_schwarz.png",
-- oder image = "drache.png" für beide Farben
```

Ohne Bild erscheint die Figur als Scheibe mit ihrem Buchstaben. Pfade außerhalb des Plugin-Ordners
(absolut oder mit `..`) werden abgelehnt.

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
| `board:legal_moves()` | alle erlaubten Züge der Seite am Zug, jeweils mit `san` (z. B. `"Qg7#"`) |
| `board:checkmate()` | steht die Seite am Zug matt? |
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

## Zwei Sprachen (`lang`)

Die globale Variable `lang` ist `"de"` oder `"en"`, je nach Sprache der Oberfläche beim Start der Partie.
So schreibt ein Plugin Namen, Meldungen und Gründe in der richtigen Sprache:

```lua
rules.set_name(lang == "en" and "Atomic Chess" or "Atomschach")
return color, lang == "en" and "King on the hill" or "König auf dem Hügel"
```

`opening_name("Italienische Partie")` liefert den Namen einer bekannten Eröffnung in der aktuellen Sprache
(„Italian Game“).

## Mit der Oberfläche sprechen (`ui`)

In `on_setup` und `on_move` gibt es die Tabelle `ui`:

| Funktion | Wirkung |
|---|---|
| `ui.message(text)` | Meldung in der Statuszeile |
| `ui.sound(name)` | Klang: `"move"`, `"capture"`, `"check"`, `"tick"`, `"end"`, `"correct"`, `"wrong"` |
| `ui.mark(x, y, [farbe])` | Kreis auf einem Feld; Farbe `"green"`, `"red"`, `"blue"` oder `"yellow"` |
| `ui.arrow(x1, y1, x2, y2, [farbe])` | Pfeil zwischen zwei Feldern |

Markierungen verschwinden mit dem nächsten Zug. Die Aufrufe wirken nur bei echten Zügen auf dem Brett:
Wenn ein Bot in Gedanken Züge durchrechnet, laufen die Plugin-Regeln zwar mit, erzeugen aber keine
Meldungen. Beispiel: `atomschach` markiert explodierte Felder rot.

## Bots und Plugin-Regeln

Die eingebaute Engine rechnet in Partien mit Plugins auf Kopien der Partie. So durchläuft jeder Zug, den
ein Bot in Gedanken prüft, dieselben Regeln wie im Spiel: `filter_moves`, `on_move` und `check_end`.
Ein Bot sieht also, dass ein Schlag im Atomschach den eigenen König sprengt oder dass ein Königszug auf
den Hügel gewinnt. Das ist langsamer als normales Schach, deshalb rechnen Bots dort höchstens 4 Halbzüge
voraus.

## Aufgaben-Plugins

`"player_moves_first": true` in plugin.json: Gegen einen Bot spielst du immer die Seite, die in der
Startstellung am Zug ist (so funktioniert `matt_in_eins` mit Weiß und Schwarz).

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
| `matt_in_eins` | Aufgaben: genau ein Mattzug, ein Versuch (nutzt `board:checkmate()`) |
| `zufallsmittelspiel` | Start mitten in der Partie: eine von 93 ausgeglichenen Stellungen aus bekannten Eröffnungen |
| `koenigshuegel` | zusätzliche Siegbedingung |
| `schlagzwang` | Züge filtern |
| `atomschach` | Brett nach jedem Zug verändern |
