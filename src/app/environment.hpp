#pragma once

// Gemeinsame Hilfen fuer Konsolen- und Grafik-Version: Pfade, Umgebungsvariablen, Stockfish-Suche.

#include <filesystem>
#include <optional>
#include <string>

namespace chess::env {

std::optional<std::string> variable(const char* name);
std::filesystem::path executableDir();

// Sucht Stockfish: CHESSENGINE_STOCKFISH, neben der exe, engines\ daneben,
// engines\ im Projektordner (zwei Ebenen ueber build\Release) und im PATH.
std::optional<std::filesystem::path> findStockfish(const std::filesystem::path& exeDir);

// Ordner fuer gespeicherte Partien: Dokumente\ChessEngine\Partien (wird angelegt).
std::filesystem::path gamesDir();

// Eigene/angepasste Bot-Profile: Dokumente\ChessEngine\Bots (wird angelegt).
std::filesystem::path userBotsDir();

// Eigene Plugins: Dokumente\ChessEngine\Plugins (wird angelegt).
std::filesystem::path userPluginsDir();

// Dokumente\ChessEngine\training.json (Aufgaben aus eigenen Fehlern, Eroeffnungs-Training)
std::filesystem::path trainingFile();

// Dokumente\ChessEngine\fortsetzen.json (beim Beenden laufende Partie)
std::filesystem::path resumeFile();

// Dokumente\ChessEngine\einstellungen.json
std::filesystem::path settingsFile();

// Dokumente\ChessEngine\profil.json (Elo und Bilanz)
std::filesystem::path profileFile();

// "2026-11-25_2154" fuer Dateinamen.
std::string timestamp();

}  // namespace chess::env
