#pragma once

#include "modules/context.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace modules {

// Edición de objetos: seleccionar, mover, redimensionar y borrar objetos de
// texto/imagen (nivel 1), insertar texto nuevo (nivel 1) y reemplazar el texto
// de un objeto existente (nivel 2).
class TextEditModule final : public Module {
public:
    [[nodiscard]] std::string_view id() const override { return "text_edit"; }
    void init(AppContext& ctx) override;
    void draw_ui() override;
    void draw_toolbar() override;
    void handle_shortcuts() override;

private:
    struct PageObjects {
        std::uint64_t revision = 0;
        std::uint64_t generation = 0;
        bool loading = false;
        std::vector<pdf::ObjectInfo> items;
    };
    enum class DragMode { none, move, resize };

    const std::vector<pdf::ObjectInfo>* objects_for(int page);
    const pdf::ObjectInfo* selected_info();
    const pdf::ObjectInfo* hit_test(int page, pdf::Point p);
    void on_page_input(const PageInput& in);
    void draw_overlay(ImDrawList& dl, const PageLayout& layout);
    bool draw_context_bar();
    void open_insert_popup(int page, pdf::Point at);
    void load_selection_into_editor(const pdf::ObjectInfo& info);
    void apply_replace();
    void delete_selected();
    [[nodiscard]] pdf::Rect handle_rect(const pdf::Rect& bounds, float scale) const;

    AppContext* m_ctx = nullptr;
    std::map<int, PageObjects> m_objects;
    std::optional<pdf::ObjectId> m_selected;
    std::optional<pdf::ObjectId> m_hovered;

    // Arrastre
    DragMode m_drag = DragMode::none;
    pdf::Point m_drag_start;
    pdf::Rect m_drag_original;
    pdf::Rect m_drag_preview;
    // Tras soltar mostramos la vista previa hasta que llega el resultado.
    std::optional<std::pair<pdf::ObjectId, pdf::Rect>> m_pending_preview;

    // Editor de propiedades (reemplazo) e inserción
    char m_text[1024] = {};
    int m_font = 0;
    float m_size = 12.0f;
    float m_color[3] = {0.0f, 0.0f, 0.0f};
    bool m_insert_popup = false;
    bool m_focus_text = false;
    int m_insert_page = 0;
    pdf::Point m_insert_at;
};

} // namespace modules
