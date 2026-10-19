#pragma once

// Tema centralizado: TODOS los colores, espaciados y redondeos de la interfaz
// salen de aquí. Para retocar el aspecto de la app solo se edita theme.cpp.

#include "core/settings.hpp"

#include <imgui.h>

namespace ui {

struct Palette {
    ImVec4 window_bg;
    ImVec4 surface;     // barras y paneles
    ImVec4 surface_alt; // hover
    ImVec4 border;
    ImVec4 text;
    ImVec4 text_dim;
    ImVec4 accent;
    ImVec4 accent_text; // texto sobre accent
    ImVec4 canvas;      // fondo detrás de las páginas
    ImVec4 page_shadow;
    ImVec4 selection; // selección de texto
    ImVec4 search_hit;
    ImVec4 search_current;
    ImVec4 object_box; // contorno de objetos editables
    ImVec4 danger;
};

struct Metrics {
    float toolbar_height = 44.0f;
    float sidebar_width = 220.0f;
    float page_gap = 16.0f; // separación vertical entre páginas
    float rounding = 6.0f;
    float icon_button = 30.0f; // lado de los botones de icono
};

class Theme {
public:
    // Aplica modo y escala DPI al estilo de ImGui.
    void apply(core::ThemeMode mode, float dpi_scale);

    [[nodiscard]] core::ThemeMode mode() const { return m_mode; }
    [[nodiscard]] const Palette& colors() const { return m_palette; }
    [[nodiscard]] const Metrics& metrics() const { return m_metrics; }
    [[nodiscard]] float scale() const { return m_scale; }

    static ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

private:
    core::ThemeMode m_mode = core::ThemeMode::dark;
    Palette m_palette{};
    Metrics m_metrics{};
    float m_scale = 1.0f;
};

} // namespace ui
