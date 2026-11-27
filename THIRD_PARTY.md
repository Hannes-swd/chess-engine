# Fremde Bestandteile

| Was | Wo | Lizenz |
|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) 1.92 (docking) | `third_party/imgui/` | MIT – `third_party/imgui/LICENSE.txt` |
| [SDL](https://github.com/libsdl-org/SDL) 2.30 | `third_party/SDL2/` | zlib – `third_party/SDL2/LICENSE.txt` |
| [Lua](https://www.lua.org) 5.4.7 (für Plugins) | `third_party/lua/` | MIT – `third_party/lua/LICENSE` |
| [Material Icons](https://github.com/google/material-design-icons) | `assets/fonts/` | Apache 2.0 – `assets/fonts/NOTICE.md` |

**Nicht enthalten:**

- **Stockfish** (GPL-3.0) läuft als eigenes Programm über UCI und wird separat heruntergeladen
  (siehe README). Der Ordner `engines/` ist in `.gitignore`.
- **Schriften** (Segoe UI, Bahnschrift, Segoe UI Symbol) werden zur Laufzeit aus `C:\Windows\Fonts`
  geladen und nicht mitgeliefert. Fehlen sie, nutzt das Programm die eingebaute ImGui-Schrift.
