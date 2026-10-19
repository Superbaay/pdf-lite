#pragma once

#include "modules/context.hpp"

#include <vector>

namespace modules {

// Anotaciones: resaltado/subrayado/tachado sobre la selección de texto, notas
// y dibujo libre. Se guardan como anotaciones PDF estándar (las ve cualquier lector).
class AnnotationsModule final : public Module {
public:
    [[nodiscard]] std::string_view id() const override { return "annotations"; }
    void init(AppContext& ctx) override;
    void draw_ui() override;
    void draw_toolbar() override;
    void handle_shortcuts() override;

private:
    void annotate_selection(pdf::AnnotKind kind);
    void on_page_input(const PageInput& in);
    void draw_overlay(ImDrawList& dl, const PageLayout& layout);
    bool draw_context_bar();
    [[nodiscard]] pdf::Color current_color() const;

    AppContext* m_ctx = nullptr;
    float m_color[3] = {1.0f, 0.86f, 0.0f}; // amarillo
    float m_ink_color[3] = {0.85f, 0.1f, 0.1f};
    // Dibujo libre en curso
    int m_ink_page = -1;
    std::vector<pdf::Point> m_stroke;
    // Nota pendiente de texto
    bool m_note_popup = false;
    int m_note_page = 0;
    pdf::Point m_note_at;
    char m_note_text[2048] = {};
};

} // namespace modules
