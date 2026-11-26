#pragma once

// Google Material Icons (assets/fonts/MaterialIcons-Regular.ttf, Apache-2.0).
// Codepoints aus MaterialIcons-Regular.codepoints. Benutzung: ImGui::Button(icon::settings)
// oder std::string(icon::flag) + " Aufgeben".

namespace chess::ui {

struct Icon {
    char s[4];
    constexpr explicit Icon(unsigned cp)
        : s{static_cast<char>(0xE0 | (cp >> 12)), static_cast<char>(0x80 | ((cp >> 6) & 0x3F)),
            static_cast<char>(0x80 | (cp & 0x3F)), 0} {}
    operator const char*() const { return s; }
};

namespace icon {
// Navigation
inline constexpr Icon play{0xea28};       // sports_esports
inline constexpr Icon analysis{0xf092};   // insights
inline constexpr Icon bot{0xf06c};        // smart_toy
inline constexpr Icon games{0xe873};      // description
inline constexpr Icon settings{0xe8b8};
inline constexpr Icon menu{0xe5d2};
inline constexpr Icon menuOpen{0xe9bd};
inline constexpr Icon darkMode{0xe51c};
inline constexpr Icon lightMode{0xe518};
inline constexpr Icon fullscreen{0xe5d0};
inline constexpr Icon fullscreenExit{0xe5d1};

// Partie
inline constexpr Icon add{0xe145};
inline constexpr Icon undo{0xe166};
inline constexpr Icon flag{0xe153};
inline constexpr Icon hint{0xe0f0};  // lightbulb
inline constexpr Icon flip{0xe8d5};  // swap_vert
inline constexpr Icon first{0xe5dc};
inline constexpr Icon prev{0xe5cb};
inline constexpr Icon next{0xe5cc};
inline constexpr Icon last{0xe5dd};
inline constexpr Icon timer{0xe425};
inline constexpr Icon history{0xe889};
inline constexpr Icon bolt{0xea0b};  // Bullet
inline constexpr Icon person{0xe7fd};
inline constexpr Icon account{0xe853};
inline constexpr Icon clear{0xe872};  // delete
inline constexpr Icon start{0xe037};  // play_arrow
inline constexpr Icon stop{0xe047};
inline constexpr Icon refresh{0xe5d5};
inline constexpr Icon casino{0xeb40};  // Zufall
inline constexpr Icon circle{0xef4a};
inline constexpr Icon circleOutline{0xe836};

// Infos / Top-Leiste
inline constexpr Icon rating{0xea3f};  // military_tech
inline constexpr Icon trophy{0xea23};  // emoji_events
inline constexpr Icon engine{0xe322};  // memory
inline constexpr Icon rated{0xef76};   // verified
inline constexpr Icon unrated{0xe14b}; // block
inline constexpr Icon trendUp{0xe8e5};
inline constexpr Icon trendDown{0xe8e3};
inline constexpr Icon trendFlat{0xe8e4};
inline constexpr Icon schedule{0xe8b5};
inline constexpr Icon info{0xe88e};
inline constexpr Icon speed{0xe9e4};
inline constexpr Icon psychology{0xea4a};
inline constexpr Icon tune{0xe429};
inline constexpr Icon balance{0xeaf6};
inline constexpr Icon fire{0xef55};
inline constexpr Icon shield{0xe9e0};
inline constexpr Icon school{0xe80c};
inline constexpr Icon visibility{0xe8f4};
inline constexpr Icon visibilityOff{0xe8f5};

// Export
inline constexpr Icon copy{0xe14d};
inline constexpr Icon save{0xe161};
inline constexpr Icon folder{0xe2c8};
inline constexpr Icon upload{0xe9fc};
inline constexpr Icon share{0xe6b8};
inline constexpr Icon check{0xe86c};
inline constexpr Icon close{0xe5cd};

// Zugbewertungen und Analyse
inline constexpr Icon star{0xe838};
inline constexpr Icon thumbUp{0xe8dc};
inline constexpr Icon checkPlain{0xe5ca};
inline constexpr Icon forward{0xe5c8};
inline constexpr Icon replay{0xe042};
inline constexpr Icon skipNext{0xe044};
inline constexpr Icon skipPrev{0xe045};
inline constexpr Icon threat{0xe1b3};  // gps_fixed
inline constexpr Icon chart{0xe6e1};   // show_chart
}  // namespace icon

}  // namespace chess::ui
