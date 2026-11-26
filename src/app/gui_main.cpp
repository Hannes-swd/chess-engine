// ChessEngine – grafische Oberflaeche (SDL2 + Dear ImGui).

#include "app/environment.hpp"
#include "ui/gui_app.hpp"
#include "ui/theme.hpp"

#include <filesystem>

#include <SDL.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    // Entwickler-Optionen: --screenshot <datei.bmp> [--moves "e4 e5 ..."] [--analyse] [--light]
    const char* screenshot = nullptr;
    std::string demoMoves;
    bool demoAnalyse = false, light = false, dark = false;
    int tab = -1, view = -1, botProfile = -1;
    std::string marks, select;
    bool hint = false, editor = false, compact = false;
    int timePreset = -1, assists = -1, previewSteps = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        else if (a == "--moves" && i + 1 < argc) demoMoves = argv[++i];
        else if (a == "--analyse") demoAnalyse = true;
        else if (a == "--light") light = true;
        else if (a == "--dark") dark = true;
        else if (a == "--tab" && i + 1 < argc) tab = std::atoi(argv[++i]);
        else if (a == "--view" && i + 1 < argc) view = std::atoi(argv[++i]);
        else if (a == "--bot" && i + 1 < argc) botProfile = std::atoi(argv[++i]);
        else if (a == "--marks" && i + 1 < argc) marks = argv[++i];
        else if (a == "--select" && i + 1 < argc) select = argv[++i];
        else if (a == "--hint") hint = true;
        else if (a == "--time" && i + 1 < argc) timePreset = std::atoi(argv[++i]);
        else if (a == "--edit") editor = true;
        else if (a == "--assist" && i + 1 < argc) assists = std::atoi(argv[++i]);
        else if (a == "--compact") compact = true;
        else if (a == "--preview" && i + 1 < argc) previewSteps = std::atoi(argv[++i]);
    }

    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    const float scale = std::max(1.0f, ImGui_ImplSDL2_GetContentScaleForDisplay(0));
    SDL_Window* window = SDL_CreateWindow("ChessEngine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          static_cast<int>(1440 * scale), static_cast<int>(880 * scale),
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(window, static_cast<int>(1000 * scale), static_cast<int>(620 * scale));
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // festes Layout, nichts zu speichern

    chess::ui::Theme theme;
    theme.scale = scale;
    theme.dark = !light;
    {
        // Icons neben der exe (assets/ wird beim Bauen kopiert), sonst im Projektordner
        const auto dir = chess::env::executableDir();
        auto font = dir / "assets" / "fonts" / "MaterialIcons-Regular.ttf";
        if (!std::filesystem::exists(font)) font = dir / ".." / ".." / "assets" / "fonts" / "MaterialIcons-Regular.ttf";
        theme.loadFonts(font.string());
    }
    theme.apply();

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    {
        chess::ui::GuiApp app(theme);
        if (light) app.overrideTheme(false);
        if (dark) app.overrideTheme(true);
        if (compact) app.requestSidebar(false);
        if (previewSteps >= 0) app.requestPreview(previewSteps);
        if (assists >= 0) app.overrideAssists(assists);
        if (botProfile >= 0) app.requestBotGame(botProfile, timePreset);
        if (editor) app.requestEditorOpen();
        if (!demoMoves.empty() || demoAnalyse) app.demo(demoMoves, demoAnalyse);
        if (!marks.empty() || !select.empty() || hint) app.demoMarks(marks, select, hint);
        if (tab >= 0) app.requestTab(tab);
        if (view >= 0) app.requestView(view);
        int frameCount = 0;
        bool running = true;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                ImGui_ImplSDL2_ProcessEvent(&e);
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE &&
                    e.window.windowID == SDL_GetWindowID(window))
                    running = false;
            }
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
                SDL_Delay(50);
                continue;
            }

            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            app.frame();
            ImGui::Render();

            SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            const ImVec4 bg = theme.p().bg;
            SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(bg.x * 255), static_cast<Uint8>(bg.y * 255),
                                   static_cast<Uint8>(bg.z * 255), 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

            // Screenshot, sobald Bot/Analyse fertig sind (spaetestens nach ~20 s)
            ++frameCount;
            if (screenshot && frameCount > 90 && (!app.busy() || frameCount > 1200)) {
                int w = 0, h = 0;
                SDL_GetRendererOutputSize(renderer, &w, &h);
                SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
                if (surf && SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels,
                                                 surf->pitch) == 0)
                    SDL_SaveBMP(surf, screenshot);
                if (surf) SDL_FreeSurface(surf);
                running = false;
            }
            SDL_RenderPresent(renderer);
        }
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
