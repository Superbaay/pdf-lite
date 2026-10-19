#include "modules/images/images_module.hpp"

#include "modules/edit_commands.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

// stb_image: una sola unidad de traducción define la implementación.
// Solo los formatos que ofrecemos, para no compilar decodificadores de más.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO // leemos el archivo nosotros (rutas Unicode)
#include <stb_image.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

namespace modules {

namespace {
constexpr int max_image_pixels = 50'000'000; // ~50 MP: evita reservar GB por un archivo raro

struct StbiFree {
    void operator()(stbi_uc* p) const noexcept { stbi_image_free(p); }
};
} // namespace

std::expected<pdf::Bitmap, std::string> load_image_bgra(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::unexpected{"no se pudo abrir la imagen"};
    }
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>{in}, {}};
    if (bytes.empty() || bytes.size() > 0x7FFFFFFF) {
        return std::unexpected{"archivo de imagen vacío o demasiado grande"};
    }
    int w = 0;
    int h = 0;
    int channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels)) {
        return std::unexpected{std::string{"formato no soportado: "} + stbi_failure_reason()};
    }
    if (w <= 0 || h <= 0 || static_cast<long long>(w) * h > max_image_pixels) {
        return std::unexpected{"la imagen es demasiado grande"};
    }
    std::unique_ptr<stbi_uc, StbiFree> rgba{
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4)};
    if (!rgba) {
        return std::unexpected{std::string{"no se pudo decodificar: "} + stbi_failure_reason()};
    }
    pdf::Bitmap out;
    out.width = w;
    out.height = h;
    out.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    const stbi_uc* src = rgba.get();
    for (std::size_t i = 0; i < out.pixels.size(); i += 4) {
        // RGBA -> BGRA. La transparencia se aplana sobre blanco: el objeto de
        // imagen de PDFium no guarda canal alfa (SMask) desde un bitmap.
        const unsigned a = src[i + 3];
        auto blend = [a](unsigned c) {
            return static_cast<std::uint8_t>((c * a + 255u * (255u - a)) / 255u);
        };
        out.pixels[i + 0] = blend(src[i + 2]);
        out.pixels[i + 1] = blend(src[i + 1]);
        out.pixels[i + 2] = blend(src[i + 0]);
        out.pixels[i + 3] = 255;
    }
    return out;
}

void ImagesModule::init(AppContext& ctx) {
    m_ctx = &ctx;
    ctx.page_context_menu.emplace_back([this](const PageInput&) {
        if (ImGui::MenuItem("Insertar imagen…", nullptr, false, m_ctx->editable())) {
            choose_and_insert();
        }
    });
}

void ImagesModule::choose_and_insert() {
    m_ctx->request_file(FileDialog::open_image,
                        [this](std::filesystem::path path) { insert(path); });
}

void ImagesModule::insert(const std::filesystem::path& path) {
    AppContext& c = *m_ctx;
    if (!c.doc.open || !c.editable()) {
        return;
    }
    auto bitmap = load_image_bgra(path);
    if (!bitmap) {
        c.notify("No se pudo cargar la imagen: " + bitmap.error(), true);
        return;
    }
    const int page = std::clamp(c.doc.current_page, 0, c.doc.page_count() - 1);
    const auto& size = c.doc.pages[static_cast<std::size_t>(page)];
    // Tamaño inicial: a 96 ppp (1 px = 0,75 pt), sin pasar del 60 % de la página.
    float w = static_cast<float>(bitmap->width) * 0.75f;
    float h = static_cast<float>(bitmap->height) * 0.75f;
    const float k = std::min({1.0f, size.width * 0.6f / w, size.height * 0.6f / h});
    w *= k;
    h *= k;
    pdf::ImageSpec spec;
    spec.rect = {(size.width - w) * 0.5f, (size.height - h) * 0.5f, (size.width + w) * 0.5f,
                 (size.height + h) * 0.5f};
    spec.bitmap = std::move(*bitmap);
    c.commands.execute(std::make_unique<InsertObjectCommand>(c, page, std::move(spec)));
    c.set_tool(Tool::edit_objects); // para poder moverla y ajustarla enseguida
    c.notify("Imagen insertada: arrástrala para colocarla", false);
}

void ImagesModule::draw_toolbar() {
    AppContext& c = *m_ctx;
    if (ui::icon_button("insert_image", ui::icon::image, "Insertar imagen (Ctrl+Shift+I)", c.theme,
                        c.editable())) {
        choose_and_insert();
    }
}

void ImagesModule::handle_shortcuts() {
    if (m_ctx->editable() &&
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_I, ImGuiInputFlags_RouteGlobal)) {
        choose_and_insert();
    }
}

} // namespace modules
