#include "ui/sound.hpp"

#include <SDL.h>

#include <cmath>

namespace chess::ui {

namespace {

constexpr int kRate = 44100;
constexpr double kPi = 3.14159265358979323846;

// Holzartiger Klick: gedaempfter Sinus mit etwas Rauschen am Anfang
std::vector<float> knock(double freq, double seconds, double decay, float volume) {
    std::vector<float> out(static_cast<size_t>(kRate * seconds));
    uint32_t noise = 12345;
    for (size_t i = 0; i < out.size(); ++i) {
        const double t = static_cast<double>(i) / kRate;
        noise = noise * 1664525u + 1013904223u;
        const double n = (static_cast<double>(noise >> 8) / 16777216.0 - 0.5) * std::exp(-t * 300.0);
        const double tone = std::sin(2 * kPi * freq * t) + 0.35 * std::sin(2 * kPi * freq * 2.7 * t);
        out[i] = static_cast<float>(volume * std::exp(-t * decay) * (tone * 0.7 + n * 0.6));
    }
    return out;
}

// Weicher Ton (fuer Schach, Ende, richtig/falsch)
std::vector<float> chime(std::initializer_list<double> freqs, double noteSeconds, float volume) {
    std::vector<float> out;
    for (double f : freqs) {
        const size_t n = static_cast<size_t>(kRate * noteSeconds);
        for (size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / kRate;
            const double env = std::min(1.0, t * 200.0) * std::exp(-t * 7.0);
            out.push_back(static_cast<float>(volume * env *
                                             (std::sin(2 * kPi * f * t) + 0.25 * std::sin(2 * kPi * f * 2 * t))));
        }
    }
    return out;
}

}  // namespace

Sound::~Sound() {
    if (device_) SDL_CloseAudioDevice(device_);
}

bool Sound::init() {
    SDL_AudioSpec want{}, have{};
    want.freq = kRate;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 512;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!device_) return false;
    samples_[static_cast<size_t>(Kind::Move)] = knock(520, 0.09, 55, 0.32f);
    samples_[static_cast<size_t>(Kind::Capture)] = knock(330, 0.14, 38, 0.42f);
    samples_[static_cast<size_t>(Kind::Check)] = chime({660, 880}, 0.11, 0.16f);
    samples_[static_cast<size_t>(Kind::Tick)] = knock(1400, 0.04, 120, 0.15f);
    samples_[static_cast<size_t>(Kind::End)] = chime({523, 659, 784}, 0.16, 0.16f);
    samples_[static_cast<size_t>(Kind::Correct)] = chime({660, 990}, 0.1, 0.15f);
    samples_[static_cast<size_t>(Kind::Wrong)] = chime({300, 240}, 0.12, 0.15f);
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void Sound::play(Kind kind) {
    if (!device_) return;
    const auto& s = samples_[static_cast<size_t>(kind)];
    if (s.empty()) return;
    // Nicht stapeln: ein neuer Klang ersetzt einen noch laufenden
    SDL_ClearQueuedAudio(device_);
    SDL_QueueAudio(device_, s.data(), static_cast<Uint32>(s.size() * sizeof(float)));
}

}  // namespace chess::ui
