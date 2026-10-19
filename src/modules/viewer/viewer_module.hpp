#pragma once

#include "modules/context.hpp"
#include "modules/page_texture_cache.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace modules {

// Lienzo principal: scroll continuo, zoom, selección y copia de texto, y
// despacho de la entrada de ratón a las herramientas de otros módulos.
class ViewerModule final : public Module {
public:
    [[nodiscard]] std::string_view id() const override { return "viewer"; }
    void init(AppContext& ctx) override;
    void draw_ui() override;
    void draw_toolbar() override;
    void handle_shortcuts() override;

private:
    enum class Fit { none, width, page };

    struct ZoomAnchor {
        int page = 0;
        pdf::Point point; // punto de la página que debe quedar fijo
        ImVec2 offset;    // su posición relativa a la esquina del lienzo
    };

    [[nodiscard]] float base_scale() const; // píxeles por punto a 100 %
    [[nodiscard]] float page_scale() const { return m_ctx->zoom * base_scale(); }
    void set_zoom(float zoom, std::optional<ImVec2> screen_anchor);
    void set_fit(Fit fit);
    void compute_layout(float avail_width, float avail_height);
    [[nodiscard]] int page_at_y(float content_y) const;
    std::optional<ZoomAnchor> anchor_at(ImVec2 screen) const;

    void draw_welcome();
    void draw_pages();
    void handle_text_selection(const PageLayout* hovered, bool item_active, bool item_clicked);
    void dispatch_tool_input(const PageLayout* hovered, bool item_hovered, bool item_active);
    void draw_context_menu();
    void copy_selection();
    void select_all_on_page(int page);

    AppContext* m_ctx = nullptr;
    std::unique_ptr<PageTextureCache> m_cache;
    Fit m_fit = Fit::width;

    // Layout (coordenadas de contenido, en píxeles)
    std::vector<float> m_page_y;
    std::vector<float> m_page_x;
    float m_content_w = 0.0f;
    float m_content_h = 0.0f;
    float m_last_scroll_x = 0.0f;
    float m_last_scroll_y = 0.0f;
    ImVec2 m_window_pos;
    ImVec2 m_view_size;

    // Peticiones de desplazamiento para el próximo cuadro
    std::optional<ZoomAnchor> m_anchor;
    std::optional<int> m_goto_page;
    std::optional<std::pair<int, pdf::Rect>> m_reveal;
    std::optional<float> m_scroll_delta;

    // Selección de texto
    std::uint64_t m_sel_token = 0;
    int m_sel_page = -1;
    int m_sel_anchor = -1;
    bool m_sel_waiting = false;

    // Entrada para herramientas
    int m_capture_page = -1;
    PageInput m_context_input;
};

} // namespace modules
