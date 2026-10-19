#pragma once

#include "modules/context.hpp"

#include <filesystem>
#include <functional>

namespace modules {

// Guardar y Guardar como. Siempre escribe una copia COMPLETA (no incremental)
// para que el contenido borrado o reemplazado no quede escondido en el archivo.
class ExportModule final : public Module {
public:
    [[nodiscard]] std::string_view id() const override { return "export"; }
    void init(AppContext& ctx) override;
    void draw_ui() override {}
    void draw_toolbar() override;
    void handle_shortcuts() override;

private:
    void save(std::function<void(bool)> done);
    void save_as(std::function<void(bool)> done);
    void save_to(const std::filesystem::path& dest, std::function<void(bool)> done);

    AppContext* m_ctx = nullptr;
    bool m_saving = false;
};

} // namespace modules
