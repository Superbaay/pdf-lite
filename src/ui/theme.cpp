#include "ui/theme.hpp"

namespace ui {

namespace {

constexpr ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
            static_cast<float>(b) / 255.0f, a};
}

Palette dark_palette() {
    Palette p{};
    p.window_bg = rgb(28, 29, 32);
    p.surface = rgb(36, 37, 41);
    p.surface_alt = rgb(50, 52, 58);
    p.border = rgb(58, 60, 66);
    p.text = rgb(232, 233, 236);
    p.text_dim = rgb(150, 153, 160);
    p.accent = rgb(94, 129, 244);
    p.accent_text = rgb(255, 255, 255);
    p.canvas = rgb(22, 23, 25);
    p.page_shadow = rgb(0, 0, 0, 0.45f);
    p.selection = rgb(94, 129, 244, 0.35f);
    p.search_hit = rgb(255, 200, 0, 0.35f);
    p.search_current = rgb(255, 120, 0, 0.55f);
    p.object_box = rgb(94, 129, 244, 0.9f);
    p.danger = rgb(235, 87, 87);
    return p;
}

Palette light_palette() {
    Palette p{};
    p.window_bg = rgb(246, 246, 248);
    p.surface = rgb(255, 255, 255);
    p.surface_alt = rgb(234, 236, 240);
    p.border = rgb(220, 222, 228);
    p.text = rgb(30, 32, 36);
    p.text_dim = rgb(110, 114, 122);
    p.accent = rgb(66, 99, 235);
    p.accent_text = rgb(255, 255, 255);
    p.canvas = rgb(232, 233, 237);
    p.page_shadow = rgb(0, 0, 0, 0.18f);
    p.selection = rgb(66, 99, 235, 0.30f);
    p.search_hit = rgb(255, 200, 0, 0.40f);
    p.search_current = rgb(255, 120, 0, 0.55f);
    p.object_box = rgb(66, 99, 235, 0.9f);
    p.danger = rgb(214, 48, 49);
    return p;
}

} // namespace

void Theme::apply(core::ThemeMode mode, float dpi_scale) {
    m_mode = mode;
    m_scale = dpi_scale;
    m_palette = mode == core::ThemeMode::light ? light_palette() : dark_palette();
    m_metrics = Metrics{};
    m_metrics.toolbar_height *= dpi_scale;
    m_metrics.sidebar_width *= dpi_scale;
    m_metrics.page_gap *= dpi_scale;
    m_metrics.rounding *= dpi_scale;
    m_metrics.icon_button *= dpi_scale;

    // Reiniciamos el estilo y lo escalamos una sola vez: ScaleAllSizes es
    // acumulativo, así que aplicarlo dos veces duplicaría los tamaños.
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle{};
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.ScrollbarSize = 12.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = {12.0f, 10.0f};
    style.FramePadding = {8.0f, 5.0f};
    style.ItemSpacing = {8.0f, 6.0f};
    style.ItemInnerSpacing = {6.0f, 4.0f};
    style.SeparatorTextBorderSize = 1.0f;
    style.ScaleAllSizes(dpi_scale);
    style.FontScaleDpi = dpi_scale;

    const Palette& p = m_palette;
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.text_dim;
    c[ImGuiCol_WindowBg] = p.window_bg;
    c[ImGuiCol_ChildBg] = {0, 0, 0, 0};
    c[ImGuiCol_PopupBg] = p.surface;
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg] = p.surface_alt;
    c[ImGuiCol_FrameBgHovered] = p.border;
    c[ImGuiCol_FrameBgActive] = p.border;
    c[ImGuiCol_TitleBg] = p.surface;
    c[ImGuiCol_TitleBgActive] = p.surface;
    c[ImGuiCol_TitleBgCollapsed] = p.surface;
    c[ImGuiCol_MenuBarBg] = p.surface;
    c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab] = p.border;
    c[ImGuiCol_ScrollbarGrabHovered] = p.text_dim;
    c[ImGuiCol_ScrollbarGrabActive] = p.accent;
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = p.accent;
    c[ImGuiCol_Button] = {0, 0, 0, 0};
    c[ImGuiCol_ButtonHovered] = p.surface_alt;
    c[ImGuiCol_ButtonActive] = p.border;
    c[ImGuiCol_Header] = p.surface_alt;
    c[ImGuiCol_HeaderHovered] = p.surface_alt;
    c[ImGuiCol_HeaderActive] = p.border;
    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_SeparatorHovered] = p.accent;
    c[ImGuiCol_SeparatorActive] = p.accent;
    c[ImGuiCol_ResizeGrip] = {0, 0, 0, 0};
    c[ImGuiCol_ResizeGripHovered] = p.accent;
    c[ImGuiCol_ResizeGripActive] = p.accent;
    c[ImGuiCol_Tab] = p.surface;
    c[ImGuiCol_TabHovered] = p.surface_alt;
    c[ImGuiCol_TabSelected] = p.surface_alt;
    c[ImGuiCol_TabSelectedOverline] = p.accent;
    c[ImGuiCol_TabDimmed] = p.surface;
    c[ImGuiCol_TabDimmedSelected] = p.surface_alt;
    c[ImGuiCol_TextSelectedBg] = p.selection;
    c[ImGuiCol_NavCursor] = p.accent;
    c[ImGuiCol_ModalWindowDimBg] = {0, 0, 0, 0.35f};
}

} // namespace ui
