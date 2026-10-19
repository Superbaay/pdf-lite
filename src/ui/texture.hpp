#pragma once

#include <imgui.h>

#include <cstdint>
#include <memory>

struct SDL_Renderer;
struct SDL_Texture;

namespace ui {

struct TextureDeleter {
    void operator()(SDL_Texture* t) const noexcept;
};

// Textura de GPU (RAII). Se mueve, no se copia.
class Texture {
public:
    Texture() = default;
    // Sube una imagen BGRA (stride = width*4) a la GPU.
    static Texture from_bgra(SDL_Renderer* renderer, int width, int height,
                             const std::uint8_t* pixels);

    [[nodiscard]] bool valid() const { return m_tex != nullptr; }
    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }
    [[nodiscard]] std::size_t bytes() const {
        return static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height) * 4;
    }
    [[nodiscard]] ImTextureID id() const;

private:
    std::unique_ptr<SDL_Texture, TextureDeleter> m_tex;
    int m_width = 0;
    int m_height = 0;
};

} // namespace ui
