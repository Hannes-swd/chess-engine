# Design-Plan

**Worum es geht:** ein Schach-Übungsraum für jemanden, der besser werden will. Hauptaufgabe: spielen,
dann verstehen, *warum* ein Zug gut oder schlecht war.

**Woher die Identität kommt:** aus der Welt des Turnierschachs – Partieformular (Notation in Spalten),
mechanische Schachuhr (große, sachliche Ziffern, Messing), Holzbrett. Nicht aus Web-App-Vorlagen.

## Farben

| Name | Hex | Rolle |
|---|---|---|
| Graphit | `#15171A` | Fläche hinter allem |
| Schiefer | `#1D2024` | Leisten, Panel |
| Knochen | `#ECE5D6` | Text, helle Felder |
| Salbei | `#7A8A82` | dunkle Felder |
| Messing | `#C9A25E` | einziger Akzent: aktive Uhr, Auswahl, Start |

Zugbewertungen bekommen eigene, gut unterscheidbare Farben (Brillant türkis, Sehr gut blau,
Bester Zug grün, Ungenau gelb, Fehler orange, Patzer rot) – nur dort, nirgends sonst.

## Schrift

- **Bahnschrift** (DIN) für alles, was gezählt oder gemessen wird: Uhren, Elo, Genauigkeit, Überschriften.
  Sie ist die Persönlichkeit der Oberfläche.
- **Segoe UI** für Fließtext (18 px, etwas kräftiger gerastert) und **Segoe UI Semibold** für
  Beschriftungen und Knöpfe. Keine dünnen oder winzigen Texte unter 14 px.

## Layout

```
┌───────────────────────────── Top-Leiste ─────────────────────────────┐
│ ≡  ♞ ChessEngine    Status der Partie              Elo  Bilanz  ◐  ⛶ │
├──────┬──────────────────────────────────────────┬────────────────────┤
│ Nav  │      Spieler oben              Uhr       │  Panel             │
│      │      ┌────────────────────┐              │  (linksbündig,     │
│      │      │       Brett        │              │   Abschnitte ohne  │
│      │      └────────────────────┘              │   Kästen-Raster)   │
│      │      Spieler unten             Uhr       │                    │
└──────┴──────────────────────────────────────────┴────────────────────┘
```

Text im Panel linksbündig. Abschnitte werden durch Abstand und eine Überschrift getrennt, nicht durch
gleich aussehende Karten.

## Prinzipien

1. **Der mutige Teil ist die Analyse:** Bewertungskurve über die ganze Breite, große Genauigkeitswerte
   in Bahnschrift, Zugsymbole direkt auf dem Brett. Alles andere bleibt ruhig.
2. **Keine Vorlagen-Merkmale:** keine Mittelpunkt-Ketten („A · B · C“), keine Versal-Etiketten,
   keine identischen Karten für alles, keine Pfeile hinter Knopftexten.
3. **Icons sitzen optisch mittig** (am tatsächlichen Glyphen gemessen, nicht an der Zeilenhöhe).
4. **Texte sagen, was passiert:** „Partie starten“, „Besseren Zug zeigen“, Fehlererklärungen in
   klaren Sätzen.
