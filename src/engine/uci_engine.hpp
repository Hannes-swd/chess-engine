#pragma once

#include "engine/engine.hpp"
#include "engine/process.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace chess {

// Spricht das UCI-Protokoll mit einem externen Programm (Stockfish, Lc0, ...).
class UciEngine : public Engine {
public:
    struct Option {
        std::string type;  // spin, check, string, combo, button
        int min = 0;
        int max = 0;
    };

    // Startet die Engine und fuehrt den UCI-Handshake durch. nullptr bei Fehler.
    static std::unique_ptr<UciEngine> launch(const std::filesystem::path& executable, std::string& error);
    ~UciEngine() override;

    std::string name() const override { return name_; }
    bool supportsPosition(const Position& pos) const override { return isStandardChessPosition(pos); }
    SearchResult search(const Position& pos, const SearchLimits& limits) override;
    void configureStrength(int skill, int elo) override;
    void newGame() override;

    bool hasOption(const std::string& name) const { return options_.count(name) != 0; }
    void setOption(const std::string& name, const std::string& value);

private:
    UciEngine() = default;
    bool handshake(std::string& error);
    bool waitFor(const std::string& token, std::chrono::milliseconds timeout);
    bool sync();
    int clampOption(const std::string& name, int value) const;

    Process proc_;
    std::string name_ = "UCI-Engine";
    std::map<std::string, Option> options_;
    int multiPv_ = 1;
    int skill_ = -1;
    int elo_ = -1;
};

}  // namespace chess
