#include "ui/image_cache.hpp"

#include <SDL.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <wincodec.h>
#include <windows.h>
#endif

#include <algorithm>
#include <vector>

namespace chess::ui {

namespace {

#ifdef _WIN32
template <class T>
void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

// Datei -> RGBA-Pixel (hoechstens maxSide x maxSide, Seitenverhaeltnis bleibt)
bool decode(const std::filesystem::path& file, int maxSide, std::vector<unsigned char>& rgba, int& w, int& h) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninit = SUCCEEDED(init);
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool ok = false;
    do {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
            break;
        if (FAILED(factory->CreateDecoderFromFilename(file.wstring().c_str(), nullptr, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder)))
            break;
        if (FAILED(decoder->GetFrame(0, &frame))) break;
        UINT fw = 0, fh = 0;
        frame->GetSize(&fw, &fh);
        if (fw == 0 || fh == 0) break;
        IWICBitmapSource* source = frame;
        const double f = std::min(1.0, static_cast<double>(maxSide) / std::max(fw, fh));
        UINT tw = std::max(1u, static_cast<UINT>(fw * f)), th = std::max(1u, static_cast<UINT>(fh * f));
        if (f < 1.0) {
            if (FAILED(factory->CreateBitmapScaler(&scaler))) break;
            if (FAILED(scaler->Initialize(frame, tw, th, WICBitmapInterpolationModeFant))) break;
            source = scaler;
        } else {
            tw = fw, th = fh;
        }
        if (FAILED(factory->CreateFormatConverter(&converter))) break;
        if (FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                         WICBitmapPaletteTypeCustom)))
            break;
        rgba.resize(static_cast<size_t>(tw) * th * 4);
        if (FAILED(converter->CopyPixels(nullptr, tw * 4, static_cast<UINT>(rgba.size()), rgba.data()))) break;
        w = static_cast<int>(tw);
        h = static_cast<int>(th);
        ok = true;
    } while (false);
    release(converter);
    release(scaler);
    release(frame);
    release(decoder);
    release(factory);
    if (uninit) CoUninitialize();
    return ok;
}
#else
bool decode(const std::filesystem::path&, int, std::vector<unsigned char>&, int&, int&) { return false; }
#endif

}  // namespace

ImageCache::~ImageCache() { clear(); }

void ImageCache::clear() {
    for (auto& [key, img] : images_)
        if (img.texture) SDL_DestroyTexture(img.texture);
    images_.clear();
}

const Image* ImageCache::get(const std::filesystem::path& file, int maxSide) {
    if (!renderer_ || file.empty()) return nullptr;
    const std::string key = file.string() + "#" + std::to_string(maxSide);
    if (auto it = images_.find(key); it != images_.end()) return it->second.texture ? &it->second : nullptr;

    Image img;
    std::error_code ec;
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (std::filesystem::exists(file, ec) && decode(file, maxSide, rgba, w, h)) {
        // RGBA im Speicher = ABGR8888 als 32-Bit-Wert (little endian)
        img.texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (img.texture) {
            SDL_UpdateTexture(img.texture, nullptr, rgba.data(), w * 4);
            SDL_SetTextureBlendMode(img.texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(img.texture, SDL_ScaleModeLinear);
            img.width = w;
            img.height = h;
        }
    }
    auto& stored = images_[key] = img;
    return stored.texture ? &stored : nullptr;
}

}  // namespace chess::ui
