#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace chess {

// Startet ein Programm (z. B. Stockfish) und spricht zeilenweise ueber stdin/stdout mit ihm.
// Ein Hintergrund-Thread liest die Ausgabe, damit readLine mit Timeout funktioniert.
class Process {
public:
    Process();
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    bool start(const std::filesystem::path& executable, std::string& error);
    bool writeLine(const std::string& line);
    // Naechste Zeile oder nullopt bei Timeout / Prozessende.
    std::optional<std::string> readLine(std::chrono::milliseconds timeout);
    // true, wenn der Prozess beendet ist und keine Zeilen mehr warten.
    bool finished() const;
    // Schliesst stdin, wartet kurz und beendet den Prozess notfalls hart.
    void stop(std::chrono::milliseconds grace = std::chrono::milliseconds(1000));

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace chess
