#include "app/app.hpp"

#include "core/version.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

namespace app {

namespace {

// Tras cada evento dibujamos unos pocos cuadros extra: ImGui necesita 1-2
// cuadros para asentar hover/animaciones. Luego el hilo se duerme en
// SDL_WaitEvent y la CPU queda en ~0 %.
constexpr int frames_after_event = 3;

} // namespace

std::expected<std::unique_ptr<App>, std::string> App::create(int /*argc*/, char** /*argv*/) {
    auto app = std::make_unique<App>(PrivateTag{});

    const char* base = SDL_GetBasePath();
    app->m_assets_dir = std::filesystem::path{base != nullptr ? base : ""} / "assets";

    // HIDDEN hasta tener el primer cuadro: evita el destello blanco al abrir.
    constexpr SDL_WindowFlags flags =
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    app->m_window.reset(SDL_CreateWindow(core::app_name, 1280, 820, flags));
    if (!app->m_window) {
        return std::unexpected{std::string{"SDL_CreateWindow: "} + SDL_GetError()};
    }
    app->m_renderer.reset(SDL_CreateRenderer(app->m_window.get(), nullptr));
    if (!app->m_renderer) {
        return std::unexpected{std::string{"SDL_CreateRenderer: "} + SDL_GetError()};
    }
    SDL_SetRenderVSync(app->m_renderer.get(), 1);

    app->m_dpi_scale = SDL_GetWindowDisplayScale(app->m_window.get());
    if (app->m_dpi_scale <= 0.0f) {
        app->m_dpi_scale = 1.0f;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    app->m_imgui_ready = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // la configuración la gestionamos nosotros
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(app->m_dpi_scale);
    style.FontScaleDpi = app->m_dpi_scale;

    ImGui_ImplSDL3_InitForSDLRenderer(app->m_window.get(), app->m_renderer.get());
    ImGui_ImplSDLRenderer3_Init(app->m_renderer.get());

    auto fonts = ui::load_fonts(app->m_assets_dir);
    if (!fonts) {
        return std::unexpected{fonts.error()};
    }
    app->m_fonts = *fonts;
    return app;
}

App::~App() {
    if (m_imgui_ready) {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
}

int App::run() {
    draw_frame();
    SDL_ShowWindow(m_window.get());

    int pending_frames = frames_after_event;
    while (m_running) {
        SDL_Event event;
        if (pending_frames <= 0) {
            // Bloquea hasta que llegue un evento: aquí la app pasa casi todo su tiempo.
            if (!SDL_WaitEvent(&event)) {
                continue;
            }
            handle_event(event);
            pending_frames = frames_after_event;
        }
        while (SDL_PollEvent(&event)) {
            handle_event(event);
            pending_frames = frames_after_event;
        }
        if (!m_running) {
            break;
        }
        if ((SDL_GetWindowFlags(m_window.get()) & SDL_WINDOW_MINIMIZED) != 0) {
            pending_frames = 0; // minimizada: no dibujar nada
            continue;
        }
        draw_frame();
        --pending_frames;
    }
    return 0;
}

void App::handle_event(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    switch (event.type) {
    case SDL_EVENT_QUIT:
        m_running = false;
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (event.window.windowID == SDL_GetWindowID(m_window.get())) {
            m_running = false;
        }
        break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
        // Se movió a un monitor con otro DPI: reescalar estilo y fuentes.
        const float scale = SDL_GetWindowDisplayScale(m_window.get());
        if (scale > 0.0f && scale != m_dpi_scale) {
            ImGuiStyle& style = ImGui::GetStyle();
            style.ScaleAllSizes(scale / m_dpi_scale);
            style.FontScaleDpi = scale;
            m_dpi_scale = scale;
        }
        break;
    }
    default:
        break;
    }
}

void App::draw_frame() {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    draw_ui();
    ImGui::Render();

    SDL_Renderer* renderer = m_renderer.get();
    const ImVec2 fb_scale = ImGui::GetIO().DisplayFramebufferScale;
    SDL_SetRenderScale(renderer, fb_scale.x, fb_scale.y);
    SDL_SetRenderDrawColorFloat(renderer, 0.11f, 0.11f, 0.12f, 1.0f);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
}

void App::draw_ui() {
    // Fase 0: solo una pantalla de bienvenida centrada.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin("##bienvenida", nullptr, flags)) {
        const char* title = "pdf-lite";
        const char* subtitle = "Visor de PDF ligero · ¿Listo? ¡Fase 0 en marcha!";

        ImGui::PushFont(m_fonts.semibold, ui::base_font_size * 2.0f);
        const ImVec2 title_size = ImGui::CalcTextSize(title);
        ImGui::SetCursorPos(
            {(viewport->WorkSize.x - title_size.x) * 0.5f, viewport->WorkSize.y * 0.42f});
        ImGui::TextUnformatted(title);
        ImGui::PopFont();

        const ImVec2 sub_size = ImGui::CalcTextSize(subtitle);
        ImGui::SetCursorPosX((viewport->WorkSize.x - sub_size.x) * 0.5f);
        ImGui::TextDisabled("%s", subtitle);

        ImGui::SetCursorPosX((viewport->WorkSize.x - ImGui::CalcTextSize("v0.0.0").x) * 0.5f);
        ImGui::TextDisabled("v%s", core::app_version);
    }
    ImGui::End();
}

} // namespace app
