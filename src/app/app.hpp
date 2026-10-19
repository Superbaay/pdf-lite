#pragma once

#include "ui/fonts.hpp"

#include <SDL3/SDL.h>

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace app {

// Deleters para que std::unique_ptr cierre recursos de SDL automáticamente.
struct WindowDeleter {
    void operator()(SDL_Window* w) const noexcept { SDL_DestroyWindow(w); }
};
struct RendererDeleter {
    void operator()(SDL_Renderer* r) const noexcept { SDL_DestroyRenderer(r); }
};
using WindowPtr = std::unique_ptr<SDL_Window, WindowDeleter>;
using RendererPtr = std::unique_ptr<SDL_Renderer, RendererDeleter>;

class App {
    // "Passkey": solo App puede crear un PrivateTag, así que solo create()
    // puede llamar al constructor público, y aun así usar std::make_unique.
    struct PrivateTag {
        explicit PrivateTag() = default;
    };

public:
    explicit App(PrivateTag /*tag*/) {}

    // Fábrica en lugar de constructor: la inicialización puede fallar y no
    // queremos excepciones cruzando la frontera con SDL.
    static std::expected<std::unique_ptr<App>, std::string> create(int argc, char** argv);

    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    int run();

private:
    void handle_event(const SDL_Event& event);
    void draw_frame();
    void draw_ui();

    // Orden importa: los miembros se destruyen en orden inverso, así que el
    // renderer se destruye antes que la ventana que lo contiene.
    WindowPtr m_window;
    RendererPtr m_renderer;
    ui::Fonts m_fonts;
    std::filesystem::path m_assets_dir;
    float m_dpi_scale = 1.0f;
    bool m_imgui_ready = false;
    bool m_running = true;
};

} // namespace app
