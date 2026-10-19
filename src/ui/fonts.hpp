#pragma once

#include <expected>
#include <filesystem>
#include <string>

struct ImFont;

namespace ui {

struct Fonts {
    ImFont* regular = nullptr;  // Inter Regular + iconos Lucide fusionados
    ImFont* semibold = nullptr; // Inter SemiBold (títulos)
};

// Tamaño base en píxeles lógicos (antes de aplicar el escalado DPI).
inline constexpr float base_font_size = 15.0f;

// Carga las fuentes empaquetadas desde `assets_dir/fonts`.
// ImGui 1.92+ rasteriza los glifos bajo demanda, así que no hace falta
// declarar rangos: á, é, ñ, ¿, ¡ se generan la primera vez que se dibujan.
std::expected<Fonts, std::string> load_fonts(const std::filesystem::path& assets_dir);

} // namespace ui
