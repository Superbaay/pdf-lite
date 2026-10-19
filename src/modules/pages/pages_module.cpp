#include "modules/pages/pages_module.hpp"

#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <numeric>

namespace modules {

namespace {

// Rotar sí es deshacible: basta con rotar en sentido contrario.
class RotatePageCommand final : public core::Command {
public:
    RotatePageCommand(AppContext& ctx, int page, int quarter_turns)
        : m_ctx{ctx}, m_page{page}, m_turns{quarter_turns} {}
    void execute() override { apply(m_turns); }
    void undo() override { apply(-m_turns); }
    [[nodiscard]] std::string description() const override { return "rotar página"; }

private:
    void apply(int turns) {
        ++m_ctx.doc.pending_edits;
        const std::uint64_t generation = m_ctx.doc.generation;
        m_ctx.engine.rotate_page(
            m_page, turns, [&ctx = m_ctx, page = m_page, generation](pdf::Result<pdf::DocInfo> r) {
                --ctx.doc.pending_edits;
                if (generation != ctx.doc.generation) {
                    return;
                }
                if (!r) {
                    ctx.notify("No se pudo rotar: " + r.error(), true);
                    return;
                }
                ctx.doc.pages = r->pages; // cambian ancho y alto de la página
                ctx.page_edited(page);
            });
    }
    AppContext& m_ctx;
    int m_page;
    int m_turns;
};

} // namespace

void PagesModule::init(AppContext& ctx) {
    m_ctx = &ctx;
    ctx.events.subscribe<PageOperation>([this](const PageOperation& op) { on_operation(op); });
}

void PagesModule::on_operation(const PageOperation& op) {
    using K = PageOperation::Kind;
    switch (op.kind) {
    case K::rotate_cw:
        rotate(op.page, 1);
        break;
    case K::rotate_ccw:
        rotate(op.page, -1);
        break;
    case K::move_up:
        move(op.page, op.page - 1);
        break;
    case K::move_down:
        move(op.page, op.page + 1);
        break;
    case K::remove:
        remove(op.page);
        break;
    case K::extract:
        extract({op.page});
        break;
    }
}

void PagesModule::apply_structure(const pdf::DocInfo& info, int focus_page) {
    AppContext& c = *m_ctx;
    c.doc.pages = info.pages;
    ++c.doc.generation;
    c.doc.page_revision.assign(c.doc.pages.size(), 0);
    c.doc.structure_dirty = true;
    c.commands.clear();
    c.doc.saved_command_revision = c.commands.revision();
    c.selection = {};
    c.events.publish(DocumentStructureChanged{});
    c.events.publish(GoToPage{std::clamp(focus_page, 0, c.doc.page_count() - 1)});
}

void PagesModule::rotate(int page, int quarter_turns) {
    AppContext& c = *m_ctx;
    if (c.editable()) {
        c.commands.execute(std::make_unique<RotatePageCommand>(c, page, quarter_turns));
    }
}

void PagesModule::remove(int page) {
    AppContext& c = *m_ctx;
    if (!c.editable() || c.doc.page_count() <= 1) {
        return;
    }
    c.confirm("Eliminar página",
              "¿Eliminar la página " + std::to_string(page + 1) +
                  "?\nEsta acción vacía el historial de deshacer.",
              [this, page] {
                  m_ctx->engine.delete_pages({page}, [this, page](pdf::Result<pdf::DocInfo> r) {
                      if (!r) {
                          m_ctx->notify("No se pudo eliminar: " + r.error(), true);
                          return;
                      }
                      apply_structure(*r, page);
                      m_ctx->notify("Página eliminada", false);
                  });
              });
}

void PagesModule::move(int from, int to) {
    AppContext& c = *m_ctx;
    if (!c.editable() || to < 0 || to >= c.doc.page_count()) {
        return;
    }
    auto run = [this, from, to] {
        m_ctx->engine.move_page(from, to, [this, to](pdf::Result<pdf::DocInfo> r) {
            if (!r) {
                m_ctx->notify("No se pudo mover: " + r.error(), true);
                return;
            }
            apply_structure(*r, to);
        });
    };
    // Si hay historial, avisamos de que se perderá; si no, movemos sin preguntar.
    if (c.commands.can_undo() || c.commands.can_redo()) {
        c.confirm("Mover página", "Reordenar páginas vacía el historial de deshacer. ¿Continuar?",
                  run);
    } else {
        run();
    }
}

void PagesModule::merge_file() {
    AppContext& c = *m_ctx;
    if (!c.editable()) {
        return;
    }
    c.request_file(FileDialog::import_pdf, [this](std::filesystem::path path) {
        const int at = m_ctx->doc.page_count();
        m_ctx->engine.import_pages(path, at, [this, at](pdf::Result<pdf::DocInfo> r) {
            if (!r) {
                m_ctx->notify("No se pudo insertar el PDF: " + r.error(), true);
                return;
            }
            const int added = static_cast<int>(r->pages.size()) - at;
            apply_structure(*r, at);
            m_ctx->notify("Se añadieron " + std::to_string(added) + " páginas al final", false);
        });
    });
}

void PagesModule::extract(std::vector<int> pages) {
    AppContext& c = *m_ctx;
    if (!c.doc.open || pages.empty()) {
        return;
    }
    c.request_file(FileDialog::save_pdf, [this,
                                          pages = std::move(pages)](std::filesystem::path dest) {
        const auto count = pages.size();
        m_ctx->engine.extract_pages(pages, dest, [this, count](pdf::Result<void> r) {
            if (!r) {
                m_ctx->notify("No se pudo extraer: " + r.error(), true);
                return;
            }
            m_ctx->notify(count == 1 ? "Página extraída a un PDF nuevo"
                                     : std::to_string(count) + " páginas extraídas a un PDF nuevo",
                          false);
        });
    });
}

void PagesModule::draw_toolbar() {
    AppContext& c = *m_ctx;
    if (ui::icon_button("pages_menu", ui::icon::files, "Páginas: rotar, eliminar, unir, dividir",
                        c.theme, c.doc.open)) {
        ImGui::OpenPopup("##pages_menu");
    }
    if (ImGui::BeginPopup("##pages_menu")) {
        const int page = c.doc.current_page;
        const bool editable = c.editable();
        ImGui::TextDisabled("Página actual: %d", page + 1);
        if (ImGui::MenuItem("Rotar a la derecha", "Ctrl+R", false, editable)) {
            rotate(page, 1);
        }
        if (ImGui::MenuItem("Rotar a la izquierda", "Ctrl+Shift+R", false, editable)) {
            rotate(page, -1);
        }
        if (ImGui::MenuItem("Mover arriba", nullptr, false, editable && page > 0)) {
            move(page, page - 1);
        }
        if (ImGui::MenuItem("Mover abajo", nullptr, false,
                            editable && page + 1 < c.doc.page_count())) {
            move(page, page + 1);
        }
        if (ImGui::MenuItem("Eliminar página", nullptr, false,
                            editable && c.doc.page_count() > 1)) {
            remove(page);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Unir: añadir otro PDF al final…", nullptr, false, editable)) {
            merge_file();
        }
        if (ImGui::MenuItem("Dividir: extraer páginas a un PDF nuevo…", nullptr, false,
                            c.doc.open)) {
            m_range_from = page + 1;
            m_range_to = page + 1;
            m_range_popup = true;
        }
        ImGui::EndPopup();
    }
}

void PagesModule::draw_ui() {
    AppContext& c = *m_ctx;
    if (c.doc.open && c.editable() && !ImGui::GetIO().WantTextInput) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_R, ImGuiInputFlags_RouteGlobal)) {
            rotate(c.doc.current_page, 1);
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_R,
                            ImGuiInputFlags_RouteGlobal)) {
            rotate(c.doc.current_page, -1);
        }
    }
    if (m_range_popup) {
        ImGui::OpenPopup("Extraer páginas");
        m_range_popup = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Extraer páginas", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const int n = c.doc.page_count();
        ImGui::TextUnformatted("Copia un rango de páginas a un PDF nuevo.");
        ImGui::SetNextItemWidth(120.0f * c.dpi_scale);
        ImGui::InputInt("Desde", &m_range_from);
        ImGui::SetNextItemWidth(120.0f * c.dpi_scale);
        ImGui::InputInt("Hasta", &m_range_to);
        m_range_from = std::clamp(m_range_from, 1, std::max(1, n));
        m_range_to = std::clamp(m_range_to, m_range_from, std::max(1, n));
        ImGui::TextDisabled("%d página(s)", m_range_to - m_range_from + 1);
        ImGui::Separator();
        if (ImGui::Button("Extraer…", {120.0f * c.dpi_scale, 0})) {
            std::vector<int> pages(static_cast<std::size_t>(m_range_to - m_range_from + 1));
            std::iota(pages.begin(), pages.end(), m_range_from - 1);
            extract(std::move(pages));
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

} // namespace modules
