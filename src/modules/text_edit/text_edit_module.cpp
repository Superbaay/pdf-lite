#include "modules/text_edit/text_edit_module.hpp"

#include "core/utf.hpp"
#include "modules/edit_commands.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace modules {

namespace {

struct FontChoice {
    const char* label;
    pdf::StandardFont font;
};

constexpr FontChoice font_choices[] = {
    {"Helvetica", pdf::StandardFont::helvetica},
    {"Helvetica negrita", pdf::StandardFont::helvetica_bold},
    {"Times", pdf::StandardFont::times},
    {"Times negrita", pdf::StandardFont::times_bold},
    {"Courier", pdf::StandardFont::courier},
    {"Inter (Unicode, incrustada)", pdf::StandardFont::embedded_inter},
};
constexpr int font_count = static_cast<int>(std::size(font_choices));
constexpr int inter_index = font_count - 1;

// Las 14 fuentes estándar de PDF solo cubren WinAnsi (Latin-1 + algunos
// símbolos). Si el texto tiene algo fuera de eso, usamos la TTF incrustada.
bool needs_unicode_font(const std::string& utf8) {
    for (char16_t c : core::utf8_to_utf16(utf8)) {
        if (c > 0xFF && c != u'€' && c != u'–' && c != u'—' && c != u'“' && c != u'”' &&
            c != u'‘' && c != u'’' && c != u'•' && c != u'…') {
            return true;
        }
    }
    return false;
}

int guess_font(const std::string& name) {
    const bool bold =
        name.find("Bold") != std::string::npos || name.find("bold") != std::string::npos;
    if (name.find("Times") != std::string::npos || name.find("Serif") != std::string::npos) {
        return bold ? 3 : 2;
    }
    if (name.find("Courier") != std::string::npos || name.find("Mono") != std::string::npos) {
        return 4;
    }
    return bold ? 1 : 0;
}

pdf::Rect normalized(pdf::Rect r) {
    return {std::min(r.x0, r.x1), std::min(r.y0, r.y1), std::max(r.x0, r.x1), std::max(r.y0, r.y1)};
}

} // namespace

void TextEditModule::init(AppContext& ctx) {
    m_ctx = &ctx;
    auto reset = [this] {
        m_objects.clear();
        m_selected.reset();
        m_hovered.reset();
        m_drag = DragMode::none;
        m_pending_preview.reset();
    };
    ctx.events.subscribe<DocumentOpened>([reset](const DocumentOpened&) { reset(); });
    ctx.events.subscribe<DocumentClosed>([reset](const DocumentClosed&) { reset(); });
    ctx.events.subscribe<DocumentStructureChanged>(
        [reset](const DocumentStructureChanged&) { reset(); });
    ctx.events.subscribe<PageEdited>([this](const PageEdited& e) {
        // Recargar los objetos de esa página y conservar la selección si sigue existiendo.
        if (auto it = m_objects.find(e.page); it != m_objects.end()) {
            it->second.revision = static_cast<std::uint64_t>(-1); // fuerza recarga
        }
    });
    ctx.events.subscribe<ToolChanged>([this](const ToolChanged& e) {
        if (e.tool != Tool::edit_objects) {
            m_selected.reset();
            m_drag = DragMode::none;
        }
    });

    ctx.page_overlays.emplace_back(
        [this](ImDrawList& dl, const PageLayout& layout) { draw_overlay(dl, layout); });
    ctx.page_input_handlers.emplace_back([this](const PageInput& in) { on_page_input(in); });
    ctx.context_bar_items.emplace_back([this] { return draw_context_bar(); });
    ctx.page_context_menu.emplace_back([this](const PageInput& in) {
        if (ImGui::MenuItem("Insertar texto aquí", nullptr, false, m_ctx->editable())) {
            open_insert_popup(in.layout.page, in.point);
        }
    });
}

const std::vector<pdf::ObjectInfo>* TextEditModule::objects_for(int page) {
    AppContext& c = *m_ctx;
    if (page < 0 || page >= c.doc.page_count()) {
        return nullptr;
    }
    auto& entry = m_objects[page];
    const std::uint64_t revision = c.doc.page_revision[static_cast<std::size_t>(page)];
    if ((entry.revision != revision || entry.generation != c.doc.generation) && !entry.loading) {
        entry.loading = true;
        const std::uint64_t generation = c.doc.generation;
        c.engine.objects(
            page, [this, page, revision, generation](pdf::Result<std::vector<pdf::ObjectInfo>> r) {
                if (generation != m_ctx->doc.generation) {
                    return;
                }
                auto& e = m_objects[page];
                e.loading = false;
                e.revision = revision;
                e.generation = generation;
                if (r) {
                    e.items = std::move(*r);
                }
                // La vista previa del arrastre se suelta cuando ya tenemos la posición real.
                if (m_pending_preview && m_pending_preview->first.page == page) {
                    m_pending_preview.reset();
                }
                // Deseleccionar si el objeto seleccionado ya no está en la página.
                if (m_selected && m_selected->page == page &&
                    std::ranges::none_of(e.items, [this](const pdf::ObjectInfo& o) {
                        return o.id == *m_selected;
                    })) {
                    m_selected.reset();
                }
            });
    }
    return &entry.items;
}

const pdf::ObjectInfo* TextEditModule::selected_info() {
    if (!m_selected) {
        return nullptr;
    }
    auto it = m_objects.find(m_selected->page);
    if (it == m_objects.end()) {
        return nullptr;
    }
    for (const auto& o : it->second.items) {
        if (o.id == *m_selected) {
            return &o;
        }
    }
    return nullptr;
}

const pdf::ObjectInfo* TextEditModule::hit_test(int page, pdf::Point p) {
    const auto* items = objects_for(page);
    if (items == nullptr) {
        return nullptr;
    }
    // Recorremos al revés: el último objeto se dibuja encima, debe ganar el clic.
    const float slop = 2.0f;
    for (auto it = items->rbegin(); it != items->rend(); ++it) {
        const pdf::Rect& b = it->bounds;
        if (p.x >= b.x0 - slop && p.x <= b.x1 + slop && p.y >= b.y0 - slop && p.y <= b.y1 + slop) {
            return &*it;
        }
    }
    return nullptr;
}

pdf::Rect TextEditModule::handle_rect(const pdf::Rect& b, float scale) const {
    const float h = 5.0f * m_ctx->dpi_scale / scale; // tamaño del tirador en puntos
    return {b.x1 - h, b.y1 - h, b.x1 + h, b.y1 + h};
}

void TextEditModule::on_page_input(const PageInput& in) {
    AppContext& c = *m_ctx;
    if (c.tool == Tool::add_text) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        if (in.clicked && c.editable()) {
            open_insert_popup(in.layout.page, in.point);
        }
        return;
    }
    if (c.tool != Tool::edit_objects) {
        return;
    }
    const int page = in.layout.page;
    const pdf::ObjectInfo* hit = in.hovered ? hit_test(page, in.point) : nullptr;
    m_hovered = hit != nullptr ? std::optional{hit->id} : std::nullopt;

    const pdf::ObjectInfo* sel = selected_info();
    const bool on_handle = sel != nullptr && sel->id.page == page &&
                           handle_rect(sel->bounds, in.layout.scale).contains(in.point);
    if (on_handle) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
    } else if (hit != nullptr) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    if (in.clicked) {
        if (on_handle && c.editable()) {
            m_drag = DragMode::resize;
            m_drag_original = sel->bounds;
        } else if (hit != nullptr) {
            m_selected = hit->id;
            load_selection_into_editor(*hit);
            m_drag = c.editable() ? DragMode::move : DragMode::none;
            m_drag_original = hit->bounds;
        } else {
            m_selected.reset();
            m_drag = DragMode::none;
        }
        m_drag_start = in.point;
        m_drag_preview = m_drag_original;
    }
    if (in.double_clicked && hit != nullptr && hit->kind == pdf::ObjectKind::text) {
        m_drag = DragMode::none;
        m_focus_text = true; // doble clic: editar el texto en la barra contextual
    }

    if (m_drag != DragMode::none && in.dragging) {
        const float dx = in.point.x - m_drag_start.x;
        const float dy = in.point.y - m_drag_start.y;
        const pdf::Rect o = m_drag_original;
        if (m_drag == DragMode::move) {
            m_drag_preview = {o.x0 + dx, o.y0 + dy, o.x1 + dx, o.y1 + dy};
        } else {
            float w = std::max(4.0f, o.width() + dx);
            float h = std::max(4.0f, o.height() + dy);
            // Proporción fija salvo con Shift (texto e imágenes no deben deformarse por accidente).
            if (!ImGui::GetIO().KeyShift) {
                const float k = std::max(w / o.width(), h / o.height());
                w = o.width() * k;
                h = o.height() * k;
            }
            m_drag_preview = {o.x0, o.y0, o.x0 + w, o.y0 + h};
        }
    }
    if (in.released && m_drag != DragMode::none && m_selected) {
        const pdf::Rect target = normalized(m_drag_preview);
        const bool changed = std::abs(target.x0 - m_drag_original.x0) > 0.5f ||
                             std::abs(target.y0 - m_drag_original.y0) > 0.5f ||
                             std::abs(target.x1 - m_drag_original.x1) > 0.5f ||
                             std::abs(target.y1 - m_drag_original.y1) > 0.5f;
        if (changed && c.editable()) {
            m_pending_preview = std::make_pair(*m_selected, target);
            c.commands.execute(
                std::make_unique<TransformObjectCommand>(c, *m_selected, m_drag_original, target));
        }
        m_drag = DragMode::none;
    }
}

void TextEditModule::draw_overlay(ImDrawList& dl, const PageLayout& layout) {
    AppContext& c = *m_ctx;
    if (c.tool != Tool::edit_objects) {
        return;
    }
    const auto* items = objects_for(layout.page);
    if (items == nullptr) {
        return;
    }
    const ui::Palette& pal = c.theme.colors();
    const ImU32 box = ui::Theme::u32(pal.object_box);
    ImVec4 faint_c = pal.object_box;
    faint_c.w = 0.35f;
    const ImU32 faint = ui::Theme::u32(faint_c);
    const float px = c.dpi_scale;

    for (const auto& o : *items) {
        const bool is_sel = m_selected && o.id == *m_selected;
        const bool is_hover = m_hovered && o.id == *m_hovered;
        pdf::Rect r = o.bounds;
        if (is_sel && m_drag != DragMode::none) {
            r = normalized(m_drag_preview);
        } else if (m_pending_preview && m_pending_preview->first == o.id) {
            r = m_pending_preview->second;
        }
        const ImVec2 a = layout.to_screen({r.x0, r.y0});
        const ImVec2 b = layout.to_screen({r.x1, r.y1});
        if (is_sel) {
            dl.AddRect({a.x - 2, a.y - 2}, {b.x + 2, b.y + 2}, box, 2.0f, 0, 2.0f * px);
            const pdf::Rect h = handle_rect(r, layout.scale);
            dl.AddRectFilled(layout.to_screen({h.x0, h.y0}), layout.to_screen({h.x1, h.y1}), box,
                             2.0f);
        } else if (is_hover) {
            dl.AddRect({a.x - 1, a.y - 1}, {b.x + 1, b.y + 1}, box, 2.0f, 0, 1.5f * px);
        } else {
            dl.AddRect(a, b, faint, 1.0f, 0, 1.0f);
        }
    }
}

void TextEditModule::load_selection_into_editor(const pdf::ObjectInfo& info) {
    if (info.kind != pdf::ObjectKind::text) {
        return;
    }
    std::strncpy(m_text, info.text.c_str(), sizeof(m_text) - 1);
    m_text[sizeof(m_text) - 1] = '\0';
    m_size = info.font_size > 0.5f ? std::round(info.font_size * 10.0f) / 10.0f : 12.0f;
    m_color[0] = static_cast<float>(info.color.r) / 255.0f;
    m_color[1] = static_cast<float>(info.color.g) / 255.0f;
    m_color[2] = static_cast<float>(info.color.b) / 255.0f;
    m_font = needs_unicode_font(info.text) ? inter_index : guess_font(info.font_name);
}

void TextEditModule::apply_replace() {
    AppContext& c = *m_ctx;
    const pdf::ObjectInfo* sel = selected_info();
    if (sel == nullptr || sel->kind != pdf::ObjectKind::text || !c.editable()) {
        return;
    }
    if (std::strlen(m_text) == 0) {
        delete_selected();
        return;
    }
    pdf::TextSpec spec;
    spec.text = m_text;
    spec.font = needs_unicode_font(spec.text) ? pdf::StandardFont::embedded_inter
                                              : font_choices[m_font].font;
    spec.size = std::clamp(m_size, 1.0f, 500.0f);
    spec.color = {static_cast<std::uint8_t>(std::lround(m_color[0] * 255)),
                  static_cast<std::uint8_t>(std::lround(m_color[1] * 255)),
                  static_cast<std::uint8_t>(std::lround(m_color[2] * 255)), 255};
    c.commands.execute(std::make_unique<ReplaceTextCommand>(c, sel->id, std::move(spec)));
    m_selected.reset(); // el objeto original desaparece; el nuevo se elige con otro clic
}

void TextEditModule::delete_selected() {
    AppContext& c = *m_ctx;
    if (!m_selected || !c.editable()) {
        return;
    }
    c.commands.execute(std::make_unique<DeleteObjectCommand>(c, *m_selected));
    m_selected.reset();
}

bool TextEditModule::draw_context_bar() {
    AppContext& c = *m_ctx;
    if (c.tool == Tool::add_text) {
        ImGui::TextDisabled("%s  Haz clic en la página donde quieras insertar texto.",
                            ui::icon::type);
        return true;
    }
    if (c.tool != Tool::edit_objects) {
        return false;
    }
    const pdf::ObjectInfo* sel = selected_info();
    if (sel == nullptr) {
        ImGui::TextDisabled("%s  Haz clic en un texto o imagen para seleccionarlo. Arrastra para "
                            "moverlo; usa la esquina para redimensionar (Shift: libre).",
                            ui::icon::pencil);
        return true;
    }
    const bool editable = c.editable();
    ImGui::BeginDisabled(!editable);
    if (sel->kind == pdf::ObjectKind::text) {
        ImGui::SetNextItemWidth(260.0f * c.dpi_scale);
        if (m_focus_text) {
            ImGui::SetKeyboardFocusHere();
            m_focus_text = false;
        }
        if (ImGui::InputText("##replace_text", m_text, sizeof(m_text),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            apply_replace();
        }
        ui::tooltip("Texto nuevo para este objeto (Enter para aplicar).\nEl reemplazo es por "
                    "objeto/línea: el resto de la página no se reacomoda.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f * c.dpi_scale);
        ImGui::Combo(
            "##font", &m_font, [](void*, int i) { return font_choices[i].label; }, nullptr,
            font_count);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f * c.dpi_scale);
        ImGui::DragFloat("##size", &m_size, 0.2f, 1.0f, 500.0f, "%.1f pt");
        ImGui::SameLine();
        ImGui::ColorEdit3("##color", m_color, ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        if (ImGui::Button("Aplicar")) {
            apply_replace();
        }
        if (sel->font_name.size() > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(original: %s, %.1f pt)", sel->font_name.c_str(), sel->font_size);
        }
    } else {
        ImGui::TextDisabled("%s  Imagen · %.0f × %.0f pt", ui::icon::image, sel->bounds.width(),
                            sel->bounds.height());
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, c.theme.colors().danger);
    if (ImGui::Button((std::string{ui::icon::trash} + " Eliminar").c_str())) {
        delete_selected();
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    return true;
}

void TextEditModule::open_insert_popup(int page, pdf::Point at) {
    m_insert_popup = true;
    m_insert_page = page;
    m_insert_at = at;
    m_text[0] = '\0';
}

void TextEditModule::draw_ui() {
    AppContext& c = *m_ctx;
    if (m_insert_popup) {
        ImGui::OpenPopup("Insertar texto");
        m_insert_popup = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Insertar texto", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(360.0f * c.dpi_scale);
        const bool enter =
            ImGui::InputTextWithHint("##new_text", "Escribe el texto…", m_text, sizeof(m_text),
                                     ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SetNextItemWidth(220.0f * c.dpi_scale);
        ImGui::Combo(
            "Fuente", &m_font, [](void*, int i) { return font_choices[i].label; }, nullptr,
            font_count);
        ImGui::SetNextItemWidth(120.0f * c.dpi_scale);
        ImGui::DragFloat("Tamaño", &m_size, 0.2f, 1.0f, 500.0f, "%.1f pt");
        ImGui::ColorEdit3("Color", m_color, ImGuiColorEditFlags_NoInputs);
        if (needs_unicode_font(m_text) && m_font != inter_index) {
            ImGui::TextDisabled("Se usará Inter: el texto tiene caracteres fuera de Latin-1.");
        }
        ImGui::Separator();
        const bool ok = ImGui::Button("Insertar", {120.0f * c.dpi_scale, 0}) || enter;
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", {120.0f * c.dpi_scale, 0}) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        } else if (ok && std::strlen(m_text) > 0 && c.editable()) {
            pdf::TextSpec spec;
            spec.text = m_text;
            spec.font = needs_unicode_font(spec.text) ? pdf::StandardFont::embedded_inter
                                                      : font_choices[m_font].font;
            spec.size = std::clamp(m_size, 1.0f, 500.0f);
            spec.color = {static_cast<std::uint8_t>(std::lround(m_color[0] * 255)),
                          static_cast<std::uint8_t>(std::lround(m_color[1] * 255)),
                          static_cast<std::uint8_t>(std::lround(m_color[2] * 255)), 255};
            spec.origin = m_insert_at;
            c.commands.execute(
                std::make_unique<InsertObjectCommand>(c, m_insert_page, std::move(spec)));
            c.set_tool(Tool::edit_objects);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void TextEditModule::draw_toolbar() {
    AppContext& c = *m_ctx;
    const bool open = c.doc.open;
    if (ui::icon_button("tool_edit", ui::icon::pencil,
                        "Editar objetos (E)\nMover, redimensionar, "
                        "reemplazar o borrar texto e imágenes",
                        c.theme, open, c.tool == Tool::edit_objects)) {
        c.set_tool(c.tool == Tool::edit_objects ? Tool::select_text : Tool::edit_objects);
    }
    ImGui::SameLine(0, 2);
    if (ui::icon_button("tool_text", ui::icon::type, "Añadir texto (T)", c.theme, open,
                        c.tool == Tool::add_text)) {
        c.set_tool(c.tool == Tool::add_text ? Tool::select_text : Tool::add_text);
    }
}

void TextEditModule::handle_shortcuts() {
    AppContext& c = *m_ctx;
    if (!c.doc.open || ImGui::GetIO().WantTextInput) {
        return;
    }
    constexpr ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiKey_E, global)) {
        c.set_tool(c.tool == Tool::edit_objects ? Tool::select_text : Tool::edit_objects);
    }
    if (ImGui::Shortcut(ImGuiKey_T, global)) {
        c.set_tool(c.tool == Tool::add_text ? Tool::select_text : Tool::add_text);
    }
    if (c.tool == Tool::edit_objects && m_selected) {
        if (ImGui::Shortcut(ImGuiKey_Delete, global) ||
            ImGui::Shortcut(ImGuiKey_Backspace, global)) {
            delete_selected();
        }
        if (ImGui::Shortcut(ImGuiKey_Escape, global)) {
            m_selected.reset();
        }
    } else if ((c.tool == Tool::edit_objects || c.tool == Tool::add_text) &&
               ImGui::Shortcut(ImGuiKey_Escape, global)) {
        c.set_tool(Tool::select_text);
    }
}

} // namespace modules
