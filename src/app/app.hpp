#pragma once

#include "core/command_stack.hpp"
#include "core/concurrent_queue.hpp"
#include "core/event_bus.hpp"
#include "core/settings.hpp"
#include "modules/context.hpp"
#include "pdf/engine.hpp"
#include "ui/fonts.hpp"
#include "ui/theme.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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

struct AppOptions {
    bool hidden = false;                 // pruebas: la ventana nunca se muestra
    std::filesystem::path settings_path; // vacío = carpeta de datos de la app
};

class App {
    // "Passkey": solo App puede crear un PrivateTag, así que solo create()
    // puede llamar al constructor público, y aun así usar std::make_unique.
    struct PrivateTag {
        explicit PrivateTag() = default;
    };

public:
    using Options = AppOptions;

    explicit App(PrivateTag /*tag*/) {}

    // Fábrica en lugar de constructor: la inicialización puede fallar y no
    // queremos excepciones cruzando la frontera con SDL.
    static std::expected<std::unique_ptr<App>, std::string> create(int argc, char** argv,
                                                                   Options options = {});

    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    int run();

    // --- API para pruebas de interfaz (tests/ui): avanzar cuadros sin bucle bloqueante.
    void step(int frames = 1);
    // Avanza hasta que el worker no tenga trabajo y los resultados estén aplicados.
    void settle();
    // Guarda el próximo cuadro dibujado como BMP.
    bool capture(const std::filesystem::path& bmp);
    [[nodiscard]] modules::AppContext& context() { return *m_ctx; }
    [[nodiscard]] SDL_Window* window() const { return m_window.get(); }
    [[nodiscard]] bool running() const { return m_running; }
    [[nodiscard]] bool modal_open() const { return m_unsaved_open || m_confirm.has_value(); }

private:
    using Clock = std::chrono::steady_clock;

    struct Toast {
        std::string text;
        bool error = false;
        Clock::time_point expires;
    };

    void init_context();
    void handle_event(const SDL_Event& event);
    bool process_background(); // tareas de diálogos + resultados del Engine + eventos
    void draw_frame();
    void draw_ui();
    void draw_toolbar();
    void draw_context_bar();
    void draw_toasts();
    void draw_modals();
    void draw_help();
    void update_title();
    void wake();
    [[nodiscard]] int wait_timeout_ms() const;

    void open_document(const std::filesystem::path& path);
    void do_open(const std::filesystem::path& path);
    void remember_current_document();
    // Ejecuta `action` si no hay cambios sin guardar; si los hay, pregunta antes.
    void guard_unsaved(std::function<void()> action);
    void request_quit();
    void apply_theme();
    void save_settings();
    void notify(std::string text, bool error);

    // --- Orden de los miembros = orden inverso de destrucción (importa):
    WindowPtr m_window;
    RendererPtr m_renderer;
    ui::Fonts m_fonts;
    ui::Theme m_theme;
    core::Settings m_settings;
    std::filesystem::path m_settings_path;
    std::filesystem::path m_assets_dir;
    core::EventBus m_events;
    core::CommandStack m_commands;
    // Módulos antes que el Engine: el Engine se destruye primero (une su hilo y
    // descarta callbacks pendientes), así ningún callback toca un módulo muerto.
    std::vector<std::unique_ptr<modules::Module>> m_modules;
    std::unique_ptr<pdf::Engine> m_engine;
    std::unique_ptr<modules::AppContext> m_ctx;

    core::ConcurrentQueue<std::function<void()>> m_tasks; // desde otros hilos
    std::atomic<bool> m_wake_pending{false};
    Uint32 m_wake_event = 0;

    std::deque<Toast> m_toasts;
    std::optional<Clock::time_point> m_settle_frame; // redibujo diferido (tooltips)

    // Diálogos modales de la app
    struct Confirm {
        std::string title;
        std::string message;
        std::function<void()> on_yes;
    };
    std::optional<Confirm> m_confirm;
    std::function<void()> m_unsaved_action; // pendiente de "¿guardar cambios?"
    bool m_unsaved_open = false;
    bool m_show_help = false;
    bool m_theme_pending = false;
    std::filesystem::path m_capture_path;

    std::string m_title;
    float m_dpi_scale = 1.0f;
    bool m_imgui_ready = false;
    bool m_running = true;
};

} // namespace app
