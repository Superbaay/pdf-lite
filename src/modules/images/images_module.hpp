#pragma once

#include "modules/context.hpp"

#include <expected>
#include <filesystem>
#include <string>

namespace modules {

// Carga un PNG/JPG/BMP y lo convierte a BGRA (lo que espera PDFium).
// Función libre para poder probarla sin interfaz.
std::expected<pdf::Bitmap, std::string> load_image_bgra(const std::filesystem::path& path);

// Insertar imágenes (nivel 1). Moverlas y redimensionarlas lo hace text_edit
// con la herramienta "Editar objetos".
class ImagesModule final : public Module {
public:
    [[nodiscard]] std::string_view id() const override { return "images"; }
    void init(AppContext& ctx) override;
    void draw_ui() override {}
    void draw_toolbar() override;
    void handle_shortcuts() override;

private:
    void choose_and_insert();
    void insert(const std::filesystem::path& path);

    AppContext* m_ctx = nullptr;
};

} // namespace modules
