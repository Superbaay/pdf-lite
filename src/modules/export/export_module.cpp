#include "modules/export/export_module.hpp"

#include "ui/icons.hpp"
#include "ui/widgets.hpp"

namespace modules {

void ExportModule::init(AppContext& ctx) {
    m_ctx = &ctx;
    ctx.save_document = [this](std::function<void(bool)> done) {
        save(std::move(done));
    };
}

void ExportModule::save(std::function<void(bool)> done) {
    if (!m_ctx->doc.open) {
        if (done) {
            done(false);
        }
        return;
    }
    save_to(m_ctx->doc.path, std::move(done));
}

void ExportModule::save_as(std::function<void(bool)> done) {
    if (!m_ctx->doc.open) {
        return;
    }
    m_ctx->request_file(FileDialog::save_pdf,
                        [this, done = std::move(done)](std::filesystem::path dest) mutable {
                            if (dest.extension().empty()) {
                                dest += ".pdf";
                            }
                            save_to(dest, std::move(done));
                        });
}

void ExportModule::save_to(const std::filesystem::path& dest, std::function<void(bool)> done) {
    AppContext& c = *m_ctx;
    if (m_saving || c.doc.pending_edits > 0) {
        c.notify("Espera a que terminen las ediciones en curso", false);
        if (done) {
            done(false);
        }
        return;
    }
    m_saving = true;
    // Revisión del historial en el momento de guardar: lo que se haga después
    // seguirá marcando el documento como modificado.
    const std::size_t revision = c.commands.revision();
    c.engine.save(dest,
                  [this, revision, dest, done = std::move(done)](pdf::Result<pdf::SaveOutcome> r) {
                      AppContext& ctx = *m_ctx;
                      m_saving = false;
                      if (!r) {
                          ctx.notify("No se pudo guardar: " + r.error(), true);
                          if (done) {
                              done(false);
                          }
                          return;
                      }
                      ctx.doc.path = r->info.path;
                      ctx.doc.structure_dirty = false;
                      if (r->reopened) {
                          // Se reabrió el archivo: los ObjectId del historial ya no existen.
                          ctx.doc.pages = r->info.pages;
                          ++ctx.doc.generation;
                          ctx.doc.page_revision.assign(ctx.doc.pages.size(), 0);
                          ctx.commands.clear();
                          ctx.doc.saved_command_revision = ctx.commands.revision();
                          ctx.events.publish(DocumentStructureChanged{});
                      } else {
                          ctx.doc.saved_command_revision = revision;
                      }
                      ctx.settings.touch_recent(ctx.doc.path, ctx.doc.current_page,
                                                ctx.fit_width ? 0.0f : ctx.zoom);
                      const auto name = dest.filename().u8string();
                      ctx.notify("Guardado: " + std::string{name.begin(), name.end()}, false);
                      if (done) {
                          done(true);
                      }
                  });
}

void ExportModule::draw_toolbar() {
    AppContext& c = *m_ctx;
    if (ui::icon_button("save", ui::icon::save, "Guardar (Ctrl+S)", c.theme,
                        c.doc.open && !m_saving && c.dirty())) {
        save(nullptr);
    }
    ImGui::SameLine(0, 2);
    if (ui::icon_button("save_as", ui::icon::save_all, "Guardar como… (Ctrl+Shift+S)", c.theme,
                        c.doc.open && !m_saving)) {
        save_as(nullptr);
    }
}

void ExportModule::handle_shortcuts() {
    if (!m_ctx->doc.open) {
        return;
    }
    constexpr ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, global)) {
        save(nullptr);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, global)) {
        save_as(nullptr);
    }
}

} // namespace modules
