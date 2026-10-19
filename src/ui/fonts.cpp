#include "ui/fonts.hpp"

#include <imgui.h>

namespace ui {

namespace {

// Área de Uso Privado de Unicode, donde Lucide pone sus iconos. Inter TAMBIÉN
// tiene glifos ahí: sin excluirlos, ImGui mostraría la letra de Inter en vez
// del icono. Y limitamos Lucide a esa zona para que no reemplace letras (su
// TTF también mapea, p. ej., la 'x'). static: ImGui guarda el puntero.
constexpr ImWchar private_use_area[] = {0xE000, 0xF8FF, 0};

ImFont* add_font(const std::filesystem::path& path, const ImFontConfig* cfg = nullptr) {
    // u8string: ImGui espera rutas UTF-8 (soporta carpetas con acentos).
    const auto utf8 = path.u8string();
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(utf8.c_str()),
                                                    base_font_size, cfg);
}

} // namespace

std::expected<Fonts, std::string> load_fonts(const std::filesystem::path& assets_dir) {
    const auto dir = assets_dir / "fonts";

    Fonts fonts;
    ImFontConfig text_cfg;
    text_cfg.GlyphExcludeRanges = private_use_area;
    fonts.regular = add_font(dir / "Inter-Regular.ttf", &text_cfg);
    if (fonts.regular == nullptr) {
        return std::unexpected{"no se pudo cargar Inter-Regular.ttf"};
    }

    // Los iconos se fusionan en la fuente regular: así un mismo texto puede
    // mezclar "icono + etiqueta" sin cambiar de fuente.
    ImFontConfig icon_cfg;
    icon_cfg.MergeMode = true;
    icon_cfg.GlyphRanges = private_use_area;
    icon_cfg.GlyphMinAdvanceX = base_font_size; // iconos monoespaciados
    icon_cfg.GlyphOffset.y = 2.0f;              // alinear con la línea base de Inter
    if (add_font(dir / "lucide.ttf", &icon_cfg) == nullptr) {
        return std::unexpected{"no se pudo cargar lucide.ttf"};
    }

    fonts.semibold = add_font(dir / "Inter-SemiBold.ttf", &text_cfg);
    if (fonts.semibold == nullptr) {
        return std::unexpected{"no se pudo cargar Inter-SemiBold.ttf"};
    }
    return fonts;
}

} // namespace ui
