#pragma once

// Bilder (PNG, JPG, BMP, GIF) fuer die Oberflaeche, z. B. Icons und Vorschaubilder von Plugins.
// Dekodiert mit der Windows Imaging Component (Teil von Windows, keine Zusatzbibliothek)
// und als SDL-Textur gespeichert. Jede Datei wird nur einmal geladen.

#include <imgui.h>

#include <filesystem>
#include <map>
#include <string>

struct SDL_Renderer;
struct SDL_Texture;

namespace chess::ui {

struct Image {
    SDL_Texture* texture = nullptr;
    int width = 0, height = 0;
    ImTextureID id() const { return static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texture)); }
};

class ImageCache {
public:
    ImageCache() = default;
    ~ImageCache();
    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    void setRenderer(SDL_Renderer* r) { renderer_ = r; }
    // nullptr, wenn die Datei fehlt, kaputt ist oder (noch) kein Renderer gesetzt ist.
    // Grosse Bilder werden auf hoechstens `maxSide` Pixel verkleinert.
    const Image* get(const std::filesystem::path& file, int maxSide = 512);
    void clear();

private:
    SDL_Renderer* renderer_ = nullptr;
    std::map<std::string, Image> images_;  // auch fehlgeschlagene (texture == nullptr), damit nicht staendig neu versucht wird
};

}  // namespace chess::ui
