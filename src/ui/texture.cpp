#include "ui/texture.hpp"

#include <SDL3/SDL.h>

namespace ui {

void TextureDeleter::operator()(SDL_Texture* t) const noexcept {
    SDL_DestroyTexture(t);
}

Texture Texture::from_bgra(SDL_Renderer* renderer, int width, int height,
                           const std::uint8_t* pixels) {
    Texture out;
    // BGRX: PDFium entrega BGRA con alfa sin usar; ignorarlo evita que un byte
    // basura vuelva transparente la página.
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRX32, SDL_TEXTUREACCESS_STATIC,
                                         width, height);
    if (tex == nullptr) {
        return out;
    }
    out.m_tex.reset(tex);
    SDL_UpdateTexture(tex, nullptr, pixels, width * 4);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    out.m_width = width;
    out.m_height = height;
    return out;
}

ImTextureID Texture::id() const {
    // El backend SDLRenderer3 de ImGui espera el SDL_Texture* como ImTextureID.
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(m_tex.get()));
}

} // namespace ui
