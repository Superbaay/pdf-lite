#include "modules/navigation/navigation_module.hpp"

#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>

namespace modules {

namespace {
constexpr std::size_t thumbs_cache_bytes = std::size_t{24} * 1024 * 1024;
constexpr float a4_ratio = 1.4142f;
} // namespace

void NavigationModule::init(AppContext& ctx) {
    m_ctx = &ctx;
    m_thumbs =
        std::make_unique<PageTextureCache>(ctx, pdf::RenderChannel::thumbnails, thumbs_cache_bytes);
    auto reset = [this] {
        m_thumbs->clear();
        m_outline.clear();
        m_outline_loaded = false;
        m_last_current = -1;
    };
    ctx.events.subscribe<DocumentOpened>([reset](const DocumentOpened&) { reset(); });
    ctx.events.subscribe<DocumentClosed>([reset](const DocumentClosed&) { reset(); });
    ctx.events.subscribe<DocumentStructureChanged>(
        [reset](const DocumentStructureChanged&) { reset(); });
}

void NavigationModule::load_outline() {
    m_outline_loaded = true;
    const std::uint64_t generation = m_ctx->doc.generation;
    m_ctx->engine.outline([this, generation](std::vector<pdf::OutlineItem> items) {
        if (generation == m_ctx->doc.generation) {
            m_outline = std::move(items);
        }
    });
}

void NavigationModule::draw_ui() {
    AppContext& c = *m_ctx;
    if (!c.sidebar_visible || !c.doc.open) {
        return;
    }
    if (!m_outline_loaded) {
        load_outline();
    }
    ImGui::SetNextWindowPos(c.sidebar_pos);
    ImGui::SetNextWindowSize(c.sidebar_size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, c.theme.colors().surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##sidebar", nullptr, flags)) {
        if (ImGui::BeginTabBar("##sidebar_tabs", ImGuiTabBarFlags_FittingPolicyShrink)) {
            if (ImGui::BeginTabItem("Miniaturas")) {
                draw_thumbnails();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Índice")) {
                ImGui::BeginChild("##outline");
                if (m_outline.empty()) {
                    ImGui::TextDisabled("Este PDF no tiene índice.");
                } else {
                    draw_outline_items(m_outline);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void NavigationModule::draw_thumbnails() {
    AppContext& c = *m_ctx;
    const ui::Palette& pal = c.theme.colors();
    ImGui::BeginChild("##thumbs");
    const float avail = ImGui::GetContentRegionAvail().x;
    const float box_w = std::max(40.0f, avail - 24.0f * c.dpi_scale);
    const float box_h = box_w * a4_ratio;
    const float label_h = ImGui::GetTextLineHeightWithSpacing();
    const float item_h = box_h + label_h + 10.0f * c.dpi_scale;
    const float fb_scale = ImGui::GetIO().DisplayFramebufferScale.x;

    // Mantener visible la miniatura de la página actual cuando ésta cambia.
    if (m_last_current != c.doc.current_page) {
        m_last_current = c.doc.current_page;
        const float y = static_cast<float>(c.doc.current_page) * item_h;
        const float top = ImGui::GetScrollY();
        const float bottom = top + ImGui::GetWindowHeight();
        if (y < top || y + item_h > bottom) {
            ImGui::SetScrollY(y - (ImGui::GetWindowHeight() - item_h) * 0.5f);
        }
    }

    // El clipper solo "visita" las filas visibles: con 1000 páginas dibujamos ~6.
    ImGuiListClipper clipper;
    clipper.Begin(c.doc.page_count(), item_h);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& size = c.doc.pages[static_cast<std::size_t>(i)];
            const float scale = std::min(box_w / size.width, box_h / size.height);
            const float w = size.width * scale;
            const float h = size.height * scale;
            ImGui::PushID(i);
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton("##thumb", {avail, item_h});
            const bool hovered = ImGui::IsItemHovered();
            if (clicked) {
                c.events.publish(GoToPage{i});
            }
            if (ImGui::BeginPopupContextItem("##thumb_ctx")) {
                using K = PageOperation::Kind;
                const bool editable = c.editable();
                if (ImGui::MenuItem("Rotar a la derecha", nullptr, false, editable)) {
                    c.events.publish(PageOperation{K::rotate_cw, i});
                }
                if (ImGui::MenuItem("Rotar a la izquierda", nullptr, false, editable)) {
                    c.events.publish(PageOperation{K::rotate_ccw, i});
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Mover arriba", nullptr, false, editable && i > 0)) {
                    c.events.publish(PageOperation{K::move_up, i});
                }
                if (ImGui::MenuItem("Mover abajo", nullptr, false,
                                    editable && i + 1 < c.doc.page_count())) {
                    c.events.publish(PageOperation{K::move_down, i});
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Extraer a un PDF nuevo…", nullptr, false, editable)) {
                    c.events.publish(PageOperation{K::extract, i});
                }
                if (ImGui::MenuItem("Eliminar página", nullptr, false,
                                    editable && c.doc.page_count() > 1)) {
                    c.events.publish(PageOperation{K::remove, i});
                }
                ImGui::EndPopup();
            }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 origin{cursor.x + (avail - w) * 0.5f,
                                cursor.y + (box_h - h) * 0.5f + 4.0f};
            const ImVec2 end{origin.x + w, origin.y + h};
            dl->AddRectFilled(origin, end, IM_COL32_WHITE);
            if (const ui::Texture* tex = m_thumbs->get(i, scale * fb_scale); tex != nullptr) {
                dl->AddImage(ImTextureRef{tex->id()}, origin, end);
            }
            const bool current = i == c.doc.current_page;
            if (current || hovered) {
                const float pad = 3.0f * c.dpi_scale;
                dl->AddRect({origin.x - pad, origin.y - pad}, {end.x + pad, end.y + pad},
                            ui::Theme::u32(current ? pal.accent : pal.border), 4.0f, 0,
                            2.0f * c.dpi_scale);
            }
            char label[16];
            std::snprintf(label, sizeof(label), "%d", i + 1);
            const ImVec2 ts = ImGui::CalcTextSize(label);
            dl->AddText({cursor.x + (avail - ts.x) * 0.5f, cursor.y + box_h + 8.0f * c.dpi_scale},
                        ui::Theme::u32(current ? pal.text : pal.text_dim), label);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void NavigationModule::draw_outline_items(const std::vector<pdf::OutlineItem>& items) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        ImGui::PushID(static_cast<int>(i));
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (item.children.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        const char* title = item.title.empty() ? "(sin título)" : item.title.c_str();
        const bool open = ImGui::TreeNodeEx("##node", flags, "%s", title);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && item.page >= 0) {
            m_ctx->events.publish(GoToPage{item.page});
        }
        if (item.page >= 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            ImGui::SetTooltip("Página %d", item.page + 1);
        }
        if (open && !item.children.empty()) {
            draw_outline_items(item.children);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void NavigationModule::draw_toolbar() {
    AppContext& c = *m_ctx;
    const bool open = c.doc.open;
    if (ui::icon_button("sidebar", ui::icon::panel_left, "Panel lateral (F4)", c.theme, open,
                        c.sidebar_visible && open)) {
        c.sidebar_visible = !c.sidebar_visible;
        c.settings.sidebar_visible = c.sidebar_visible;
    }
    ImGui::SameLine(0, 2);
    if (ui::icon_button("prev", ui::icon::chevron_left, "Página anterior (←)", c.theme,
                        open && c.doc.current_page > 0)) {
        c.events.publish(GoToPage{c.doc.current_page - 1});
    }
    ImGui::SameLine(0, 2);
    // Campo de página: muestra la actual y acepta un número + Enter.
    if (!m_editing_page) {
        std::snprintf(m_page_input, sizeof(m_page_input), "%d", open ? c.doc.current_page + 1 : 0);
    }
    ImGui::SetNextItemWidth(46.0f * c.dpi_scale);
    ImGui::BeginDisabled(!open);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                         (c.theme.metrics().icon_button - ImGui::GetFrameHeight()) * 0.5f);
    if (m_focus_page_input) {
        ImGui::SetKeyboardFocusHere();
        m_focus_page_input = false;
    }
    if (ImGui::InputText("##page", m_page_input, sizeof(m_page_input),
                         ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll)) {
        int page = 0;
        const char* end = m_page_input + std::strlen(m_page_input);
        if (std::from_chars(m_page_input, end, page).ec == std::errc{} && page >= 1) {
            c.events.publish(GoToPage{std::min(page, c.doc.page_count()) - 1});
        }
    }
    m_editing_page = ImGui::IsItemActive();
    ImGui::EndDisabled();
    ui::tooltip("Ir a página (Ctrl+G)");
    ImGui::SameLine();
    ImGui::TextDisabled("/ %d", open ? c.doc.page_count() : 0);
    ImGui::SameLine(0, 4);
    if (ui::icon_button("next", ui::icon::chevron_right, "Página siguiente (→)", c.theme,
                        open && c.doc.current_page + 1 < c.doc.page_count())) {
        c.events.publish(GoToPage{c.doc.current_page + 1});
    }
}

void NavigationModule::handle_shortcuts() {
    AppContext& c = *m_ctx;
    constexpr ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiKey_F4, global) && c.doc.open) {
        c.sidebar_visible = !c.sidebar_visible;
        c.settings.sidebar_visible = c.sidebar_visible;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_G, global) && c.doc.open) {
        m_focus_page_input = true; // se enfoca al dibujar la barra (ver draw_toolbar)
    }
}

} // namespace modules
