#include "modules/viewer/viewer_module.hpp"

#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include <SDL3/SDL_clipboard.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <string>

namespace modules {

namespace {

constexpr float min_zoom = 0.1f;
constexpr float max_zoom = 8.0f;
constexpr float zoom_step = 1.15f;
constexpr std::size_t view_cache_bytes = std::size_t{192} * 1024 * 1024;
constexpr float zoom_presets[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f};

ImU32 col(const ImVec4& c) {
    return ui::Theme::u32(c);
}

} // namespace

void ViewerModule::init(AppContext& ctx) {
    m_ctx = &ctx;
    m_cache = std::make_unique<PageTextureCache>(ctx, pdf::RenderChannel::view, view_cache_bytes);

    ctx.events.subscribe<DocumentOpened>([this](const DocumentOpened&) {
        m_cache->clear();
        m_ctx->selection = {};
        m_sel_page = -1;
        // Restaurar zoom y última página vistos de este archivo.
        if (const auto* recent = m_ctx->settings.find_recent(m_ctx->doc.path)) {
            if (recent->zoom > 0.0f) {
                m_fit = Fit::none;
                m_ctx->zoom = std::clamp(recent->zoom, min_zoom, max_zoom);
            } else {
                m_fit = Fit::width;
            }
            m_goto_page =
                std::clamp(recent->last_page, 0, std::max(0, m_ctx->doc.page_count() - 1));
        } else {
            m_fit = Fit::width;
            m_goto_page = 0;
        }
    });
    ctx.events.subscribe<DocumentClosed>([this](const DocumentClosed&) {
        m_cache->clear();
        m_ctx->selection = {};
    });
    ctx.events.subscribe<DocumentStructureChanged>([this](const DocumentStructureChanged&) {
        m_cache->clear();
        m_ctx->selection = {};
    });
    ctx.events.subscribe<PageEdited>([this](const PageEdited& e) {
        if (m_ctx->selection.page == e.page) {
            m_ctx->selection = {}; // el texto pudo cambiar: la selección ya no vale
        }
    });
    ctx.events.subscribe<GoToPage>([this](const GoToPage& e) { m_goto_page = e.page; });
    ctx.events.subscribe<RevealRect>(
        [this](const RevealRect& e) { m_reveal = std::make_pair(e.page, e.rect); });
}

float ViewerModule::base_scale() const {
    // 100 % = tamaño físico: 1 pt = 1/72", la pantalla "estándar" es de 96 ppp.
    return (96.0f / 72.0f) * m_ctx->dpi_scale;
}

void ViewerModule::set_zoom(float zoom, std::optional<ImVec2> screen_anchor) {
    zoom = std::clamp(zoom, min_zoom, max_zoom);
    if (std::abs(zoom - m_ctx->zoom) < 1e-4f) {
        return;
    }
    // Ancla: por defecto, el centro de la vista.
    const ImVec2 at = screen_anchor.value_or(
        ImVec2{m_window_pos.x + m_view_size.x * 0.5f, m_window_pos.y + m_view_size.y * 0.5f});
    m_anchor = anchor_at(at);
    m_fit = Fit::none;
    m_ctx->zoom = zoom;
    m_cache->cancel_pending();
}

void ViewerModule::set_fit(Fit fit) {
    m_anchor = anchor_at({m_window_pos.x + m_view_size.x * 0.5f, m_window_pos.y + 1.0f});
    m_fit = fit;
    m_cache->cancel_pending();
}

std::optional<ViewerModule::ZoomAnchor> ViewerModule::anchor_at(ImVec2 screen) const {
    if (!m_ctx->doc.open || m_page_y.empty()) {
        return std::nullopt;
    }
    const float content_y = screen.y - m_window_pos.y + m_last_scroll_y;
    const float content_x = screen.x - m_window_pos.x + m_last_scroll_x;
    const int page = page_at_y(content_y);
    const float s = page_scale();
    ZoomAnchor a;
    a.page = page;
    a.point = {(content_x - m_page_x[static_cast<std::size_t>(page)]) / s,
               (content_y - m_page_y[static_cast<std::size_t>(page)]) / s};
    a.offset = {screen.x - m_window_pos.x, screen.y - m_window_pos.y};
    return a;
}

int ViewerModule::page_at_y(float content_y) const {
    // m_page_y está ordenado: búsqueda binaria de la última página que empieza antes de y.
    auto it = std::upper_bound(m_page_y.begin(), m_page_y.end(), content_y);
    if (it == m_page_y.begin()) {
        return 0;
    }
    return static_cast<int>(std::distance(m_page_y.begin(), it)) - 1;
}

void ViewerModule::compute_layout(float avail_w, float avail_h) {
    const auto& pages = m_ctx->doc.pages;
    const float gap = m_ctx->theme.metrics().page_gap;
    if (pages.empty()) {
        return;
    }
    // Zoom de ajuste (en función del tamaño disponible).
    float max_w = 0.0f;
    for (const auto& p : pages) {
        max_w = std::max(max_w, p.width);
    }
    if (m_fit == Fit::width) {
        m_ctx->zoom = std::clamp((avail_w - 2 * gap) / (max_w * base_scale()), min_zoom, max_zoom);
    } else if (m_fit == Fit::page) {
        const auto& cur = pages[static_cast<std::size_t>(
            std::clamp(m_ctx->doc.current_page, 0, static_cast<int>(pages.size()) - 1))];
        const float zw = (avail_w - 2 * gap) / (cur.width * base_scale());
        const float zh = (avail_h - 2 * gap) / (cur.height * base_scale());
        m_ctx->zoom = std::clamp(std::min(zw, zh), min_zoom, max_zoom);
    }
    m_ctx->fit_width = m_fit == Fit::width;

    const float s = page_scale();
    m_content_w = std::max(avail_w, max_w * s + 2 * gap);
    m_page_y.resize(pages.size());
    m_page_x.resize(pages.size());
    float y = gap;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        m_page_y[i] = y;
        m_page_x[i] = std::floor((m_content_w - pages[i].width * s) * 0.5f);
        y += pages[i].height * s + gap;
    }
    m_content_h = y;
}

void ViewerModule::draw_ui() {
    AppContext& c = *m_ctx;
    const ui::Palette& p = c.theme.colors();
    ImGui::SetNextWindowPos(c.canvas_pos);
    ImGui::SetNextWindowSize(c.canvas_size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, p.canvas);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_HorizontalScrollbar;
    // Ctrl+rueda es zoom: impedimos que además desplace la vista.
    if (ImGui::GetIO().KeyCtrl) {
        flags |= ImGuiWindowFlags_NoScrollWithMouse;
    }

    if (c.doc.open) {
        const float sb = ImGui::GetStyle().ScrollbarSize;
        compute_layout(c.canvas_size.x - sb, c.canvas_size.y - sb);
        const float s = page_scale();
        const float gap = c.theme.metrics().page_gap;
        // Peticiones de desplazamiento: se resuelven ANTES de Begin con
        // SetNextWindowScroll para que el cuadro ya se dibuje en su sitio.
        std::optional<ImVec2> scroll;
        if (m_anchor && m_anchor->page < static_cast<int>(m_page_y.size())) {
            const auto i = static_cast<std::size_t>(m_anchor->page);
            scroll = ImVec2{m_page_x[i] + m_anchor->point.x * s - m_anchor->offset.x,
                            m_page_y[i] + m_anchor->point.y * s - m_anchor->offset.y};
            m_anchor.reset();
        }
        if (m_goto_page && !m_page_y.empty()) {
            const int page = std::clamp(*m_goto_page, 0, static_cast<int>(m_page_y.size()) - 1);
            scroll = ImVec2{m_last_scroll_x, m_page_y[static_cast<std::size_t>(page)] - gap * 0.5f};
            c.doc.current_page = page;
            m_goto_page.reset();
        }
        if (m_reveal && m_reveal->first < static_cast<int>(m_page_y.size())) {
            const auto i = static_cast<std::size_t>(m_reveal->first);
            const pdf::Rect& r = m_reveal->second;
            const float top = m_page_y[i] + r.y0 * s;
            const float bottom = m_page_y[i] + r.y1 * s;
            float sy = m_last_scroll_y;
            if (top < m_last_scroll_y || bottom > m_last_scroll_y + m_view_size.y) {
                sy = top - m_view_size.y * 0.33f;
            }
            float sx = m_last_scroll_x;
            const float left = m_page_x[i] + r.x0 * s;
            const float right = m_page_x[i] + r.x1 * s;
            if (left < m_last_scroll_x || right > m_last_scroll_x + m_view_size.x) {
                sx = left - m_view_size.x * 0.25f;
            }
            scroll = ImVec2{sx, sy};
            m_reveal.reset();
        }
        if (m_scroll_delta) {
            scroll = ImVec2{m_last_scroll_x, m_last_scroll_y + *m_scroll_delta};
            m_scroll_delta.reset();
        }
        if (scroll) {
            ImGui::SetNextWindowScroll({std::max(0.0f, scroll->x), std::max(0.0f, scroll->y)});
        }
    }

    ImGui::Begin("##canvas", nullptr, flags);
    m_window_pos = ImGui::GetWindowPos();
    m_view_size = ImGui::GetWindowSize();
    if (c.doc.open) {
        draw_pages();
    } else {
        c.visible_pages.clear();
        draw_welcome();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void ViewerModule::draw_welcome() {
    AppContext& c = *m_ctx;
    const ImVec2 size = ImGui::GetWindowSize();
    const float column = std::min(size.x - 32.0f, 460.0f * c.dpi_scale);
    ImGui::SetCursorPos({(size.x - column) * 0.5f, size.y * 0.22f});
    ImGui::BeginGroup();
    ImGui::PushFont(c.fonts.semibold, ui::base_font_size * 2.0f);
    ImGui::TextUnformatted("pdf-lite");
    ImGui::PopFont();
    ImGui::TextDisabled("Visor y editor de PDF ligero. Arrastra un archivo a la ventana.");
    ImGui::Dummy({0, 12.0f * c.dpi_scale});
    ImGui::PushStyleColor(ImGuiCol_Button, c.theme.colors().accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c.theme.colors().accent);
    ImGui::PushStyleColor(ImGuiCol_Text, c.theme.colors().accent_text);
    const std::string open_label = std::string{ui::icon::folder_open} + "  Abrir PDF…";
    if (ImGui::Button(open_label.c_str(), {0, 34.0f * c.dpi_scale})) {
        c.request_file(FileDialog::open_pdf,
                       [&c](std::filesystem::path path) { c.open_document(std::move(path)); });
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+O");

    if (!c.settings.recents.empty()) {
        ImGui::Dummy({0, 18.0f * c.dpi_scale});
        ImGui::PushFont(c.fonts.semibold, 0.0f);
        ImGui::TextUnformatted("Recientes");
        ImGui::PopFont();
        ImGui::Separator();
        std::optional<std::filesystem::path> to_open;
        std::optional<std::filesystem::path> to_remove;
        for (std::size_t i = 0; i < c.settings.recents.size(); ++i) {
            const auto& r = c.settings.recents[i];
            std::error_code ec;
            const bool exists = std::filesystem::exists(r.path, ec);
            const auto name = r.path.filename().u8string();
            const auto folder = r.path.parent_path().u8string();
            ImGui::PushID(static_cast<int>(i));
            const std::string label =
                std::string{ui::icon::file_text} + "  " + std::string{name.begin(), name.end()};
            ImGui::BeginDisabled(!exists);
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowOverlap,
                                  {column - 30.0f * c.dpi_scale, 0})) {
                to_open = r.path;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled |
                                     ImGuiHoveredFlags_ForTooltip)) {
                const std::string tip = std::string{folder.begin(), folder.end()} +
                                        (exists ? "" : "\n(archivo no encontrado)");
                ImGui::SetTooltip("%s", tip.c_str());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ui::icon::x)) {
                to_remove = r.path;
            }
            ui::tooltip("Quitar de recientes");
            ImGui::PopID();
        }
        if (to_remove) {
            c.settings.remove_recent(*to_remove);
        }
        if (to_open) {
            c.open_document(*to_open);
        }
    }
    ImGui::EndGroup();
}

void ViewerModule::draw_pages() {
    AppContext& c = *m_ctx;
    const ui::Palette& pal = c.theme.colors();
    const float s = page_scale();
    const float fb_scale = ImGui::GetIO().DisplayFramebufferScale.x;

    // Un botón invisible del tamaño del contenido: da estado de hover/arrastre y
    // fija el área desplazable.
    ImGui::SetCursorPos({0, 0});
    ImGui::InvisibleButton("##pages", {m_content_w, m_content_h},
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool item_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool item_active = ImGui::IsItemActive();
    const bool item_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool item_right_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    m_last_scroll_x = ImGui::GetScrollX();
    m_last_scroll_y = ImGui::GetScrollY();
    const float view_top = m_last_scroll_y;
    const float view_bottom = view_top + m_view_size.y;

    // Página actual: la que ocupa el 30 % superior de la vista.
    c.doc.current_page = page_at_y(view_top + m_view_size.y * 0.3f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    c.visible_pages.clear();
    const int count = c.doc.page_count();
    const int first = page_at_y(view_top);
    const ImVec2 mouse = ImGui::GetMousePos();
    int hovered_index = -1; // índice en visible_pages (un puntero se invalidaría al crecer)
    for (int i = std::max(0, first - 1); i < count; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        const auto& size = c.doc.pages[idx];
        const float y = m_page_y[idx];
        if (y > view_bottom + m_view_size.y) {
            break; // más allá de una pantalla de margen: no hace falta
        }
        const float w = size.width * s;
        const float h = size.height * s;
        const bool visible = y + h >= view_top && y <= view_bottom;
        // Prefetch: pedimos también la página anterior/siguiente a la vista.
        const ui::Texture* tex = m_cache->get(i, s * fb_scale);
        if (!visible) {
            continue;
        }
        const ImVec2 origin{m_window_pos.x + m_page_x[idx] - m_last_scroll_x,
                            m_window_pos.y + y - m_last_scroll_y};
        const ImVec2 end{origin.x + w, origin.y + h};
        const float shadow = 3.0f * c.dpi_scale;
        dl->AddRectFilled({origin.x + shadow, origin.y + shadow}, {end.x + shadow, end.y + shadow},
                          col(pal.page_shadow), 2.0f);
        dl->AddRectFilled(origin, end, IM_COL32_WHITE);
        if (tex != nullptr && tex->valid()) {
            dl->AddImage(ImTextureRef{tex->id()}, origin, end);
        } else {
            const char* loading = "Cargando…";
            const ImVec2 ts = ImGui::CalcTextSize(loading);
            dl->AddText({origin.x + (w - ts.x) * 0.5f, origin.y + (h - ts.y) * 0.5f},
                        IM_COL32(150, 150, 150, 255), loading);
        }

        PageLayout layout{i, origin, s};
        c.visible_pages.push_back(layout);

        // Resultados de búsqueda
        for (int hidx = 0; hidx < static_cast<int>(c.search.hits.size()); ++hidx) {
            const auto& hit = c.search.hits[static_cast<std::size_t>(hidx)];
            if (hit.page != i) {
                continue;
            }
            const ImU32 color = col(hidx == c.search.current ? pal.search_current : pal.search_hit);
            for (const auto& r : hit.rects) {
                dl->AddRectFilled(layout.to_screen({r.x0, r.y0}), layout.to_screen({r.x1, r.y1}),
                                  color, 1.0f);
            }
        }
        // Selección de texto
        if (c.selection.page == i) {
            for (const auto& r : c.selection.rects) {
                dl->AddRectFilled(layout.to_screen({r.x0, r.y0}), layout.to_screen({r.x1, r.y1}),
                                  col(pal.selection));
            }
        }
        // Capas de otros módulos (objetos editables, herramientas...)
        dl->PushClipRect(origin, end, true);
        for (const auto& overlay : c.page_overlays) {
            overlay(*dl, layout);
        }
        dl->PopClipRect();

        if (mouse.x >= origin.x && mouse.x < end.x && mouse.y >= origin.y && mouse.y < end.y) {
            hovered_index = static_cast<int>(c.visible_pages.size()) - 1;
        }
    }
    const PageLayout* hovered =
        hovered_index >= 0 ? &c.visible_pages[static_cast<std::size_t>(hovered_index)] : nullptr;

    if (item_right_clicked && hovered != nullptr) {
        m_context_input = PageInput{};
        m_context_input.layout = *hovered;
        m_context_input.point = hovered->to_page(mouse);
        ImGui::OpenPopup("##page_ctx");
    }
    draw_context_menu();

    if (c.tool == Tool::select_text) {
        if (item_hovered && hovered != nullptr) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        }
        handle_text_selection(hovered, item_active, item_clicked);
    } else {
        dispatch_tool_input(hovered, item_hovered, item_active);
    }

    // Ctrl+rueda: zoom anclado al cursor.
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && io.MouseWheel != 0.0f && ImGui::IsWindowHovered()) {
        const float factor = io.MouseWheel > 0 ? zoom_step : 1.0f / zoom_step;
        set_zoom(c.zoom * factor, mouse);
    }
}

void ViewerModule::handle_text_selection(const PageLayout* hovered, bool item_active,
                                         bool item_clicked) {
    AppContext& c = *m_ctx;
    if (item_clicked) {
        // Nuevo clic: empezar una selección desde cero.
        ++m_sel_token;
        c.selection = {};
        m_sel_anchor = -1;
        m_sel_waiting = false;
        m_sel_page = hovered != nullptr ? hovered->page : -1;
        if (m_sel_page >= 0) {
            const std::uint64_t token = m_sel_token;
            c.engine.char_index_at(m_sel_page, hovered->to_page(ImGui::GetMousePos()),
                                   [this, token](int idx) {
                                       if (token == m_sel_token && m_sel_anchor < 0) {
                                           m_sel_anchor = idx;
                                       }
                                   });
        }
        return;
    }
    if (!item_active || m_sel_page < 0 || m_sel_waiting ||
        !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
        return;
    }
    // Buscamos la geometría de la página donde empezó la selección (aunque el
    // ratón ya esté fuera de ella).
    auto it = std::ranges::find_if(c.visible_pages,
                                   [this](const PageLayout& l) { return l.page == m_sel_page; });
    if (it == c.visible_pages.end()) {
        return;
    }
    pdf::Point pt = it->to_page(ImGui::GetMousePos());
    const auto& size = c.doc.pages[static_cast<std::size_t>(m_sel_page)];
    pt.x = std::clamp(pt.x, 0.0f, size.width);
    pt.y = std::clamp(pt.y, 0.0f, size.height);

    // Solo una petición en vuelo: si el ratón se mueve rápido saltamos posiciones
    // intermedias en vez de encolar cientos de búsquedas.
    m_sel_waiting = true;
    const std::uint64_t token = m_sel_token;
    const int page = m_sel_page;
    c.engine.char_index_at(page, pt, [this, token, page](int idx) {
        if (token != m_sel_token) {
            return;
        }
        if (idx < 0) {
            m_sel_waiting = false;
            return;
        }
        if (m_sel_anchor < 0) {
            m_sel_anchor = idx;
        }
        m_ctx->engine.text_range(
            page, m_sel_anchor, idx, [this, token](pdf::Result<pdf::TextSelection> sel) {
                if (token != m_sel_token) {
                    return;
                }
                m_sel_waiting = false;
                if (sel) {
                    m_ctx->selection = {sel->page, std::move(sel->rects), std::move(sel->text)};
                }
            });
    });
}

void ViewerModule::dispatch_tool_input(const PageLayout* hovered, bool item_hovered,
                                       bool item_active) {
    AppContext& c = *m_ctx;
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left) && m_capture_page >= 0;

    const PageLayout* target = nullptr;
    if (m_capture_page >= 0) {
        auto it = std::ranges::find_if(
            c.visible_pages, [this](const PageLayout& l) { return l.page == m_capture_page; });
        if (it != c.visible_pages.end()) {
            target = &*it;
        }
    } else if (item_hovered) {
        target = hovered;
    }
    if (target == nullptr) {
        if (released) {
            m_capture_page = -1;
        }
        return;
    }

    PageInput input;
    input.layout = *target;
    input.point = target->to_page(mouse);
    input.hovered = hovered != nullptr && hovered->page == target->page;
    input.clicked = item_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    input.double_clicked = item_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    input.dragging = item_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f);
    input.released = released;
    if (input.clicked) {
        m_capture_page = target->page;
    }
    for (const auto& handler : c.page_input_handlers) {
        handler(input);
    }
    if (released) {
        m_capture_page = -1;
    }
}

void ViewerModule::draw_context_menu() {
    AppContext& c = *m_ctx;
    if (!ImGui::BeginPopup("##page_ctx")) {
        return;
    }
    if (ImGui::MenuItem("Copiar", "Ctrl+C", false, !c.selection.empty())) {
        copy_selection();
    }
    if (ImGui::MenuItem("Seleccionar texto de la página", "Ctrl+A")) {
        select_all_on_page(m_context_input.layout.page);
    }
    for (const auto& item : c.page_context_menu) {
        item(m_context_input);
    }
    ImGui::EndPopup();
}

void ViewerModule::copy_selection() {
    if (!m_ctx->selection.empty() && !m_ctx->selection.text.empty()) {
        SDL_SetClipboardText(m_ctx->selection.text.c_str());
        m_ctx->notify("Texto copiado", false);
    }
}

void ViewerModule::select_all_on_page(int page) {
    m_ctx->set_tool(Tool::select_text);
    const std::uint64_t token = ++m_sel_token;
    m_sel_page = page;
    m_ctx->engine.text_range(page, 0, INT_MAX, [this, token](pdf::Result<pdf::TextSelection> sel) {
        if (token == m_sel_token && sel) {
            m_ctx->selection = {sel->page, std::move(sel->rects), std::move(sel->text)};
        }
    });
}

void ViewerModule::draw_toolbar() {
    AppContext& c = *m_ctx;
    const bool open = c.doc.open;
    if (ui::icon_button("zoom_out", ui::icon::zoom_out, "Alejar (Ctrl+-)", c.theme, open)) {
        set_zoom(c.zoom / zoom_step, std::nullopt);
    }
    ImGui::SameLine(0, 2);
    char label[32];
    std::snprintf(label, sizeof(label), "%d %%##zoom", static_cast<int>(std::lround(c.zoom * 100)));
    ImGui::BeginDisabled(!open);
    if (ImGui::Button(label, {64.0f * c.dpi_scale, c.theme.metrics().icon_button})) {
        ImGui::OpenPopup("##zoom_menu");
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("##zoom_menu")) {
        if (ImGui::MenuItem("Ajustar al ancho", "Ctrl+0", m_fit == Fit::width)) {
            set_fit(Fit::width);
        }
        if (ImGui::MenuItem("Ajustar a la página", "Ctrl+2", m_fit == Fit::page)) {
            set_fit(Fit::page);
        }
        ImGui::Separator();
        for (float z : zoom_presets) {
            char item[16];
            std::snprintf(item, sizeof(item), "%d %%", static_cast<int>(std::lround(z * 100)));
            const bool selected = m_fit == Fit::none && std::abs(c.zoom - z) < 0.001f;
            if (ImGui::MenuItem(item, z == 1.0f ? "Ctrl+1" : nullptr, selected)) {
                set_zoom(z, std::nullopt);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine(0, 2);
    if (ui::icon_button("zoom_in", ui::icon::zoom_in, "Acercar (Ctrl++)", c.theme, open)) {
        set_zoom(c.zoom * zoom_step, std::nullopt);
    }
    ImGui::SameLine(0, 2);
    if (ui::icon_button("fit", ui::icon::move_horizontal, "Ajustar al ancho (Ctrl+0)", c.theme,
                        open, m_fit == Fit::width)) {
        set_fit(m_fit == Fit::width ? Fit::page : Fit::width);
    }
}

void ViewerModule::handle_shortcuts() {
    AppContext& c = *m_ctx;
    if (!c.doc.open || ImGui::GetIO().WantTextInput) {
        return;
    }
    constexpr ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Equal, global) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd, global)) {
        set_zoom(c.zoom * zoom_step, std::nullopt);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Minus, global) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract, global)) {
        set_zoom(c.zoom / zoom_step, std::nullopt);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_0, global)) {
        set_fit(Fit::width);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_1, global)) {
        set_zoom(1.0f, std::nullopt);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_2, global)) {
        set_fit(Fit::page);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, global)) {
        copy_selection();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A, global)) {
        select_all_on_page(c.doc.current_page);
    }
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        return;
    }
    // Navegación con teclado
    const float page_step = m_view_size.y * 0.9f;
    const float line_step = 60.0f * c.dpi_scale;
    if (ImGui::Shortcut(ImGuiKey_PageDown, global) || ImGui::Shortcut(ImGuiKey_Space, global)) {
        m_scroll_delta = page_step;
    }
    if (ImGui::Shortcut(ImGuiKey_PageUp, global) ||
        ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_Space, global)) {
        m_scroll_delta = -page_step;
    }
    if (ImGui::Shortcut(ImGuiKey_DownArrow, global | ImGuiInputFlags_Repeat)) {
        m_scroll_delta = line_step;
    }
    if (ImGui::Shortcut(ImGuiKey_UpArrow, global | ImGuiInputFlags_Repeat)) {
        m_scroll_delta = -line_step;
    }
    if (ImGui::Shortcut(ImGuiKey_RightArrow, global | ImGuiInputFlags_Repeat)) {
        m_goto_page = std::min(c.doc.current_page + 1, c.doc.page_count() - 1);
    }
    if (ImGui::Shortcut(ImGuiKey_LeftArrow, global | ImGuiInputFlags_Repeat)) {
        m_goto_page = std::max(c.doc.current_page - 1, 0);
    }
    if (ImGui::Shortcut(ImGuiKey_Home, global)) {
        m_goto_page = 0;
    }
    if (ImGui::Shortcut(ImGuiKey_End, global)) {
        m_goto_page = c.doc.page_count() - 1;
    }
    if (ImGui::Shortcut(ImGuiKey_Escape, global)) {
        ++m_sel_token;
        c.selection = {};
    }
}

} // namespace modules
