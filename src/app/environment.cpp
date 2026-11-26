#include "app/environment.hpp"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace chess::env {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

std::optional<std::string> variable(const char* name) {
#ifdef _MSC_VER
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) != 0 || !buf) return std::nullopt;
    std::string v(buf);
    free(buf);
    return v;
#else
    const char* v = std::getenv(name);
    return v ? std::optional<std::string>(v) : std::nullopt;
#endif
}

fs::path executableDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return fs::path(buf).parent_path();
#endif
    return fs::current_path();
}

std::optional<fs::path> findStockfish(const fs::path& exeDir) {
    if (auto e = variable("CHESSENGINE_STOCKFISH"); e && fs::exists(*e)) return fs::path(*e);

    const std::vector<fs::path> dirs = {exeDir, exeDir / "engines", exeDir / ".." / ".." / "engines"};
    for (const fs::path& dir : dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            const std::string name = lower(entry.path().filename().string());
            if (entry.is_regular_file() && name.find("stockfish") != std::string::npos &&
                entry.path().extension() == ".exe")
                return fs::canonical(entry.path(), ec);
        }
    }
    if (auto path = variable("PATH")) {
        std::stringstream ss(*path);
        std::string dir;
        while (std::getline(ss, dir, ';')) {
            std::error_code ec;
            const fs::path candidate = fs::path(dir) / "stockfish.exe";
            if (!dir.empty() && fs::exists(candidate, ec)) return candidate;
        }
    }
    return std::nullopt;
}

namespace {

fs::path userDir(const char* sub) {
    fs::path base;
    if (auto profile = variable("USERPROFILE"))
        base = fs::path(*profile) / "Documents";
    else
        base = executableDir();
    const fs::path dir = base / "ChessEngine" / sub;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

}  // namespace

fs::path gamesDir() { return userDir("Partien"); }

fs::path userBotsDir() { return userDir("Bots"); }

fs::path settingsFile() { return userDir("Partien").parent_path() / "einstellungen.json"; }

fs::path profileFile() { return userDir("Partien").parent_path() / "profil.json"; }

std::string timestamp() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H%M%S", &tm);
    return buf;
}

}  // namespace chess::env
