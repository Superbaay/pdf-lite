#include "modules/annotations/annotations_module.hpp"

#include "modules/edit_commands.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include <cmath>
#include <cstring>

namespace modules {

namespace {
pdf::Color to_color(const float c[3]) {
    return {static_cast<std::uint8_t>(std::lround(c[0] * 255)),
            static_cast<std::uint8_t>(std::lround(c[1] * 255)),
            static_cast<std::uint8_t>(std::lround(c[2] * 255)), 255};
}
} // namespace

void AnnotationsModule::init(AppContext& ctx) {
    m_ctx = &ctx;
    ctx.page_input_handlers.emplace_back([this](const PageInput& in) { on_page_input(in); });
    ctx.page_overlays.emplace_back(
        [this](ImDrawList& dl, const PageLayout& layout) { draw_overlay(dl, layout); });
    ctx.context_bar_items.emplace_back([this] { return draw_context_bar(); });
    ctx.page_context_menu.emplace_back([this](const PageInput& in) {
        const bool can = m_ctx->editable() && !m_ctx->selection.empty();
        ImGui::Separator();
        if (ImGui::MenuItem("Resaltar selección", "Ctrl+H", false, can)) {
            annotate_selection(pdf::AnnotKind::highlight);
        }
        if (ImGui::MenuItem("Subrayar selección", "Ctrl+U", false, can)) {
            annotate_selection(pdf::AnnotKind::underline);
        }
        if (ImGui::MenuItem("Tachar selección", nullptr, false, can)) {
            annotate_selection(pdf::AnnotKind::strikeout);
        }
        if (ImGui::MenuItem("Añadir nota aquí", nullptr, false, m_ctx->editable())) {
            m_note_popup = true;
            m_note_page = in.layout.page;
            m_note_at = in.point;
            m_note_text[0] = '\0';
        }
    });
    ctx.events.subscribe<ToolChanged>([this](const ToolChanged&) {
        m_stroke.clear();
        m_ink_page = -1;
    });
}

pdf::Color AnnotationsModule::current_color() const {
    return to_color(m_color);
}

void AnnotationsModule::annotate_selection(pdf::AnnotKind kind) {
    AppContext& c = *m_ctx;
    if (c.selection.empty() || !c.editable()) {
        c.notify("Primero selecciona texto con el ratón", false);
        return;
    }
    pdf::AnnotSpec spec;
    spec.kind = kind;
    spec.color = kind == pdf::AnnotKind::highlight ? current_color() : to_color(m_ink_color);
    spec.rects = c.selection.rects;
    const char* what = kind == pdf::AnnotKind::highlight   ? "resaltar"
                       : kind == pdf::AnnotKind::underline ? "subrayar"
                                                           : "tachar";
    c.commands.execute(
        std::make_unique<AddAnnotationCommand>(c, c.selection.page, std::move(spec), what));
    c.selection = {};
}

void AnnotationsModule::on_page_input(const PageInput& in) {
    AppContext& c = *m_ctx;
    if (c.tool == Tool::note) {
        if (in.clicked && c.editable()) {
            m_note_popup = true;
            m_note_page = in.layout.page;
            m_note_at = in.point;
            m_note_text[0] = '\0';
        }
        return;
    }
    if (c.tool != Tool::ink) {
        return;
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (in.clicked) {
        m_ink_page = in.layout.page;
        m_stroke.clear();
        m_stroke.push_back(in.point);
    }
    if (in.dragging && m_ink_page == in.layout.page) {
        // Solo añadimos puntos que se alejan un poco del anterior: trazo más ligero.
        const auto& last = m_stroke.back();
        const float min_step = 1.5f / in.layout.scale * c.dpi_scale;
        if (std::hypot(in.point.x - last.x, in.point.y - last.y) > min_step) {
            m_stroke.push_back(in.point);
        }
    }
    if (in.released && m_ink_page == in.layout.page) {
        if (m_stroke.size() >= 2 && c.editable()) {
            pdf::AnnotSpec spec;
            spec.kind = pdf::AnnotKind::ink;
            spec.color = to_color(m_ink_color);
            spec.strokes.push_back(std::move(m_stroke));
            c.commands.execute(
                std::make_unique<AddAnnotationCommand>(c, m_ink_page, std::move(spec), "dibujar"));
        }
        m_stroke.clear();
        m_ink_page = -1;
    }
}

void AnnotationsModule::draw_overlay(ImDrawList& dl, const PageLayout& layout) {
    if (m_ink_page != layout.page || m_stroke.size() < 2) {
        return;
    }
    // Vista previa del trazo mientras se dibuja.
    std::vector<ImVec2> points;
    points.reserve(m_stroke.size());
    for (const auto& p : m_stroke) {
        points.push_back(layout.to_screen(p));
    }
    const ImVec4 c{m_ink_color[0], m_ink_color[1], m_ink_color[2], 1.0f};
    dl.AddPolyline(points.data(), static_cast<int>(points.size()), ui::Theme::u32(c),
                   ImDrawFlags_None, 2.0f * layout.scale);
}

bool AnnotationsModule::draw_context_bar() {
    AppContext& c = *m_ctx;
    if (c.tool == Tool::ink) {
        ImGui::TextDisabled("%s  Dibuja con el ratón sobre la página.", ui::icon::pen_tool);
        ImGui::SameLine();
        ImGui::ColorEdit3("Color del trazo", m_ink_color, ImGuiColorEditFlags_NoInputs);
        return true;
    }
    if (c.tool == Tool::note) {
        ImGui::TextDisabled("%s  Haz clic donde quieras dejar una nota.", ui::icon::sticky_note);
        ImGui::SameLine();
        ImGui::ColorEdit3("Color de la nota", m_color, ImGuiColorEditFlags_NoInputs);
        return true;
    }
    return false;
}

void AnnotationsModule::draw_ui() {
    AppContext& c = *m_ctx;
    if (m_note_popup) {
        ImGui::OpenPopup("Nueva nota");
        m_note_popup = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Nueva nota", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputTextMultiline("##note", m_note_text, sizeof(m_note_text),
                                  {360.0f * c.dpi_scale, 120.0f * c.dpi_scale});
        ImGui::Separator();
        if (ImGui::Button("Añadir", {120.0f * c.dpi_scale, 0}) && std::strlen(m_note_text) > 0 &&
            c.editable()) {
            pdf::AnnotSpec spec;
            spec.kind = pdf::AnnotKind::note;
            spec.color = current_color();
            spec.position = m_note_at;
            spec.contents = m_note_text;
            c.commands.execute(
                std::make_unique<AddAnnotationCommand>(c, m_note_page, std::move(spec), "nota"));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", {120.0f * c.dpi_scale, 0}) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void AnnotationsModule::draw_toolbar() {
    AppContext& c = *m_ctx;
    const bool can_sel = c.editable() && !c.selection.empty();
    if (ui::icon_button("hl", ui::icon::highlighter,
                        "Resaltar selección (Ctrl+H)\nSelecciona texto primero", c.theme,
                        can_sel)) {
        annotate_selection(pdf::AnnotKind::highlight);
    }
    ImGui::SameLine(0, 2);
    if (ui::icon_button("ul", ui::icon::underline, "Subrayar selección (Ctrl+U)", c.theme,
                        can_sel)) {
        annotate_selection(pdf::AnnotKind::underline);
    }
    ImGui::SameLine(0, 2);
    if (ui::icon_button("note", ui::icon::sticky_note, "Nota (N)", c.theme, c.doc.open,
                        c.tool == Tool::note)) {
        c.set_tool(c.tool == Tool::note ? Tool::select_text : Tool::note);
    }
    ImGui::SameLine(0, 2);
    if (ui::icon_button("ink", ui::icon::pen_tool, "Dibujo libre (D)", c.theme, c.doc.open,
                        c.tool == Tool::ink)) {
        c.set_tool(c.tool == Tool::ink ? Tool::select_text : Tool::ink);
    }
}

void AnnotationsModule::handle_shortcuts() {
    AppContext& c = *m_ctx;
    if (!c.doc.open || ImGui::GetIO().WantTextInput) {
        return;
    }
    constexpr ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_H, global)) {
        annotate_selection(pdf::AnnotKind::highlight);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_U, global)) {
        annotate_selection(pdf::AnnotKind::underline);
    }
    if (ImGui::Shortcut(ImGuiKey_N, global)) {
        c.set_tool(c.tool == Tool::note ? Tool::select_text : Tool::note);
    }
    if (ImGui::Shortcut(ImGuiKey_D, global)) {
        c.set_tool(c.tool == Tool::ink ? Tool::select_text : Tool::ink);
    }
    if ((c.tool == Tool::note || c.tool == Tool::ink) && ImGui::Shortcut(ImGuiKey_Escape, global)) {
        c.set_tool(Tool::select_text);
    }
}

} // namespace modules
