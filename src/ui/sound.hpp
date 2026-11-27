#pragma once

// Kurze, leise Klaenge fuer Zug, Schlag, Schach, Zeitnot und Partieende.
// Werden beim Start synthetisiert (keine Audiodateien noetig) und ueber SDL abgespielt.

#include <array>
#include <cstdint>
#include <vector>

namespace chess::ui {

class Sound {
public:
    enum class Kind { Move, Capture, Check, Tick, End, Correct, Wrong, Count };

    Sound() = default;
    ~Sound();
    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;

    // SDL_INIT_AUDIO muss vorher erfolgt sein. false, wenn kein Audiogeraet da ist (dann stumm).
    bool init();
    void play(Kind kind);
    bool ready() const { return device_ != 0; }

private:
    uint32_t device_ = 0;
    std::array<std::vector<float>, static_cast<size_t>(Kind::Count)> samples_;
};

}  // namespace chess::ui
