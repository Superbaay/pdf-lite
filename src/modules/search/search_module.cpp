#include "modules/search/search_module.hpp"

#include "ui/icons.hpp"
#include "ui/widgets.hpp"

namespace modules {

void SearchModule::init(AppContext& ctx) {
    m_ctx = &ctx;
    ctx.events.subscribe<DocumentOpened>([this](const DocumentOpened&) { clear(); });
    ctx.events.subscribe<DocumentClosed>([this](const DocumentClosed&) { clear(); });
    ctx.events.subscribe<DocumentStructureChanged>(
        [this](const DocumentStructureChanged&) { clear(); });
    // Si se edita una página, las posiciones de los resultados pueden cambiar:
    // forzamos a repetir la búsqueda la próxima vez que se pulse Enter.
    ctx.events.subscribe<PageEdited>([this](const PageEdited&) { m_last_query.clear(); });
}

void SearchModule::clear() {
    ++m_token;
    m_ctx->search = {};
    m_last_query.clear();
    m_searching = false;
}

void SearchModule::run_search() {
    const std::string query{m_query};
    if (query.empty()) {
        clear();
        return;
    }
    // Misma búsqueda: Enter avanza al siguiente resultado.
    if (query == m_last_query && m_match_case == m_last_case && m_whole_word == m_last_word) {
        if (!m_ctx->search.hits.empty()) {
            go_to(m_ctx->search.current + 1);
        }
        return;
    }
    m_last_query = query;
    m_last_case = m_match_case;
    m_last_word = m_whole_word;
    m_searching = true;
    const std::uint64_t token = ++m_token;
    m_ctx->engine.search(query, m_match_case, m_whole_word,
                         [this, token](std::vector<pdf::SearchHit> hits) {
                             if (token != m_token) {
                                 return; // llegó tarde: hubo otra búsqueda después
                             }
                             m_searching = false;
                             m_ctx->search.hits = std::move(hits);
                             m_ctx->search.current = -1;
                             if (m_ctx->search.hits.empty()) {
                                 m_ctx->notify("Sin resultados", false);
                                 return;
                             }
                             // Empezar por el primer resultado desde la página actual.
                             int first = 0;
                             for (int i = 0; i < static_cast<int>(m_ctx->search.hits.size()); ++i) {
                                 if (m_ctx->search.hits[static_cast<std::size_t>(i)].page >=
                                     m_ctx->doc.current_page) {
                                     first = i;
                                     break;
                                 }
                             }
                             go_to(first);
                         });
}

void SearchModule::go_to(int index) {
    auto& s = m_ctx->search;
    if (s.hits.empty()) {
        return;
    }
    const int n = static_cast<int>(s.hits.size());
    s.current = ((index % n) + n) % n; // circular
    const auto& hit = s.hits[static_cast<std::size_t>(s.current)];
    if (!hit.rects.empty()) {
        pdf::Rect box = hit.rects.front();
        for (const auto& r : hit.rects) {
            box.x0 = std::min(box.x0, r.x0);
            box.y0 = std::min(box.y0, r.y0);
            box.x1 = std::max(box.x1, r.x1);
            box.y1 = std::max(box.y1, r.y1);
        }
        m_ctx->events.publish(RevealRect{hit.page, box});
    } else {
        m_ctx->events.publish(GoToPage{hit.page});
    }
}

void SearchModule::draw_toolbar() {
    AppContext& c = *m_ctx;
    const bool open = c.doc.open;
    ImGui::BeginDisabled(!open);
    ImGui::SetNextItemWidth(190.0f * c.dpi_scale);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                         (c.theme.metrics().icon_button - ImGui::GetFrameHeight()) * 0.5f);
    if (m_focus) {
        ImGui::SetKeyboardFocusHere();
        m_focus = false;
    }
    const std::string hint = std::string{ui::icon::search} + "  Buscar…";
    const bool enter = ImGui::InputTextWithHint("##search", hint.c_str(), m_query, sizeof(m_query),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    const bool active = ImGui::IsItemActive();
    if (enter) {
        if (ImGui::GetIO().KeyShift && std::string{m_query} == m_last_query) {
            go_to(c.search.current - 1);
        } else {
            run_search();
        }
        m_focus = true; // Enter quita el foco en ImGui: lo devolvemos para seguir navegando
    }
    // Esc desactiva el campo en el MISMO cuadro: miramos también IsItemDeactivated.
    if ((active || ImGui::IsItemDeactivated()) && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_query[0] = '\0';
        clear();
    }
    ui::tooltip("Buscar en el documento (Ctrl+F)\nEnter: siguiente · Shift+Enter: anterior");
    ImGui::EndDisabled();

    ImGui::SameLine(0, 4);
    if (m_searching) {
        ImGui::TextDisabled("…");
    } else if (!c.search.hits.empty()) {
        ImGui::TextDisabled("%d/%d", c.search.current + 1, static_cast<int>(c.search.hits.size()));
    }
    ImGui::SameLine(0, 2);
    const bool has = !c.search.hits.empty();
    if (ui::icon_button("search_prev", ui::icon::arrow_up, "Anterior (Shift+F3)", c.theme, has)) {
        go_to(c.search.current - 1);
    }
    ImGui::SameLine(0, 2);
    if (ui::icon_button("search_next", ui::icon::arrow_down, "Siguiente (F3)", c.theme, has)) {
        go_to(c.search.current + 1);
    }
    ImGui::SameLine(0, 2);
    if (ui::icon_button("search_opts", "Aa", "Opciones de búsqueda", c.theme, open,
                        m_match_case || m_whole_word)) {
        ImGui::OpenPopup("##search_opts");
    }
    if (ImGui::BeginPopup("##search_opts")) {
        ImGui::Checkbox("Distinguir mayúsculas", &m_match_case);
        ImGui::Checkbox("Palabra completa", &m_whole_word);
        ImGui::EndPopup();
    }
}

void SearchModule::handle_shortcuts() {
    AppContext& c = *m_ctx;
    if (!c.doc.open) {
        return;
    }
    constexpr ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F, global)) {
        m_focus = true;
    }
    if (ImGui::Shortcut(ImGuiKey_F3, global)) {
        if (c.search.hits.empty()) {
            run_search();
        } else {
            go_to(c.search.current + 1);
        }
    }
    if (ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_F3, global)) {
        go_to(c.search.current - 1);
    }
}

} // namespace modules
