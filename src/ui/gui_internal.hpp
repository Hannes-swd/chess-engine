#pragma once

// Gemeinsame Kleinigkeiten fuer gui_app.cpp und layout.cpp.

namespace chess::ui::detail {

struct TimePreset {
    const char* label;
    const char* kind;
    double minutes;  // 0 = ohne Uhr, < 0 = eigene
    int increment;
};

inline constexpr TimePreset kTimePresets[] = {
    {"Ohne Uhr", "", 0, 0},     {"1+0", "Bullet", 1, 0},      {"3+2", "Blitz", 3, 2},   {"5+0", "Blitz", 5, 0},
    {"10+0", "Schnell", 10, 0}, {"15+10", "Schnell", 15, 10}, {"30+0", "Lang", 30, 0}, {"Eigene", "", -1, 0},
};
inline constexpr int kTimePresetCount = static_cast<int>(sizeof(kTimePresets) / sizeof(kTimePresets[0]));

}  // namespace chess::ui::detail
