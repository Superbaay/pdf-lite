#include "app/app.hpp"

#include "app/dialogs.hpp"
#include "core/utf.hpp"
#include "core/version.hpp"
#include "modules/annotations/annotations_module.hpp"
#include "modules/export/export_module.hpp"
#include "modules/images/images_module.hpp"
#include "modules/navigation/navigation_module.hpp"
#include "modules/pages/pages_module.hpp"
#include "modules/search/search_module.hpp"
#include "modules/text_edit/text_edit_module.hpp"
#include "modules/viewer/viewer_module.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cctype>

namespace app {

namespace {

// Tras cada evento dibujamos unos pocos cuadros extra: ImGui necesita 1-2
// cuadros para asentar hover/animaciones. Luego el hilo se duerme en
// SDL_WaitEvent y la CPU queda en ~0 %.
constexpr int frames_after_event = 3;
// Redibujo único un rato después de la última entrada, para que aparezcan los
// tooltips (ImGui los muestra tras un retraso sin que haya eventos nuevos).
constexpr auto settle_delay = std::chrono::milliseconds{650};
constexpr auto toast_duration = std::chrono::milliseconds{3200};
constexpr int caret_blink_ms = 530;

std::string to_utf8(const std::filesystem::path& p) {
    return core::path_to_utf8(p);
}

// Registro de módulos en tiempo de compilación: para añadir uno nuevo basta con
// crear su carpeta en src/modules/ y añadirlo aquí. El orden es el de dibujo.
std::vector<std::unique_ptr<modules::Module>> create_modules() {
    std::vector<std::unique_ptr<modules::Module>> list;
    list.push_back(std::make_unique<modules::ViewerModule>());
    list.push_back(std::make_unique<modules::NavigationModule>());
    list.push_back(std::make_unique<modules::SearchModule>());
    list.push_back(std::make_unique<modules::TextEditModule>());
    list.push_back(std::make_unique<modules::ImagesModule>());
    list.push_back(std::make_unique<modules::AnnotationsModule>());
    list.push_back(std::make_unique<modules::PagesModule>());
    list.push_back(std::make_unique<modules::ExportModule>());
    return list;
}

modules::Module* find_module(const std::vector<std::unique_ptr<modules::Module>>& list,
                             std::string_view id) {
    for (const auto& m : list) {
        if (m->id() == id) {
            return m.get();
        }
    }
    return nullptr;
}

} // namespace

// --- Creación -----------------------------------------------------------------

std::expected<std::unique_ptr<App>, std::string> App::create(int argc, char** argv,
                                                             Options options) {
    auto app = std::make_unique<App>(PrivateTag{});

    const char* base = SDL_GetBasePath();
    // SDL devuelve UTF-8: convertir con path_from_utf8 (ver core/utf.hpp) o una
    // carpeta con acentos rompería la carga de fuentes.
    app->m_assets_dir = core::path_from_utf8(base != nullptr ? base : "") / "assets";

    // Configuración en la carpeta de datos de la app (%APPDATA%\pdf-lite\).
    if (!options.settings_path.empty()) {
        app->m_settings_path = options.settings_path;
        if (auto loaded = core::load_settings(app->m_settings_path)) {
            app->m_settings = std::move(*loaded);
        }
    } else if (char* pref = SDL_GetPrefPath("pdf-lite", "pdf-lite"); pref != nullptr) {
        const std::string pref_utf8{pref};
        SDL_free(pref);
        app->m_settings_path = core::path_from_utf8(pref_utf8) / "settings.json";
        if (auto loaded = core::load_settings(app->m_settings_path)) {
            app->m_settings = std::move(*loaded);
        }
    }

    // HIDDEN hasta tener el primer cuadro: evita el destello blanco al abrir.
    constexpr SDL_WindowFlags flags =
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    app->m_window.reset(SDL_CreateWindow(core::app_name, 1280, 860, flags));
    if (!app->m_window) {
        return std::unexpected{std::string{"SDL_CreateWindow: "} + SDL_GetError()};
    }
    SDL_SetWindowMinimumSize(app->m_window.get(), 720, 480);
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
    app->apply_theme();

    ImGui_ImplSDL3_InitForSDLRenderer(app->m_window.get(), app->m_renderer.get());
    ImGui_ImplSDLRenderer3_Init(app->m_renderer.get());

    auto fonts = ui::load_fonts(app->m_assets_dir);
    if (!fonts) {
        return std::unexpected{fonts.error()};
    }
    app->m_fonts = *fonts;

    // Evento propio con el que el worker de PDF despierta al bucle principal.
    app->m_wake_event = SDL_RegisterEvents(1);
    App* self = app.get();
    app->m_engine = std::make_unique<pdf::Engine>(app->m_assets_dir / "fonts" / "Inter-Regular.ttf",
                                                  [self] { self->wake(); });
    app->init_context();

    static_cast<void>(options.hidden); // la ventana nace oculta; run() la muestra
    if (argc > 1 && argv[1] != nullptr) {
        const std::string arg{argv[1]};
        app->open_document(core::path_from_utf8(arg));
    }
    return app;
}

void App::init_context() {
    m_ctx = std::make_unique<modules::AppContext>(*m_engine, m_events, m_commands, m_settings,
                                                  m_theme, m_fonts, m_renderer.get(), m_dpi_scale);
    modules::AppContext& c = *m_ctx;
    c.sidebar_visible = m_settings.sidebar_visible;
    c.request_file = [this](modules::FileDialog kind,
                            std::function<void(std::filesystem::path)> on_chosen) {
        DialogKind dk = DialogKind::open_pdf;
        switch (kind) {
        case modules::FileDialog::open_pdf:
        case modules::FileDialog::import_pdf:
            dk = DialogKind::open_pdf;
            break;
        case modules::FileDialog::save_pdf:
            dk = DialogKind::save_pdf;
            break;
        case modules::FileDialog::open_image:
            dk = DialogKind::open_image;
            break;
        }
        const auto folder =
            m_ctx->doc.open ? m_ctx->doc.path.parent_path() : std::filesystem::path{};
        // El diálogo puede responder desde otro hilo: reenviamos al hilo de UI.
        show_file_dialog(m_window.get(), dk, folder,
                         [this, on_chosen = std::move(on_chosen)](std::filesystem::path path) {
                             m_tasks.push([on_chosen, path = std::move(path)] { on_chosen(path); });
                             wake();
                         });
    };
    c.notify = [this](std::string text, bool error) {
        notify(std::move(text), error);
    };
    c.confirm = [this](std::string title, std::string message, std::function<void()> on_yes) {
        m_confirm = Confirm{std::move(title), std::move(message), std::move(on_yes)};
    };
    c.open_document = [this](std::filesystem::path path) {
        open_document(path);
    };

    m_modules = create_modules();
    for (auto& module : m_modules) {
        module->init(c);
    }
}

App::~App() {
    save_settings();
    // El Engine se para antes de destruir ImGui y el renderer (ver orden de miembros).
    m_ctx.reset();
    m_engine.reset();
    m_modules.clear();
    if (m_imgui_ready) {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
}

void App::wake() {
    // Coalescemos: si ya hay un aviso en la cola de SDL, no ponemos otro.
    if (!m_wake_pending.exchange(true)) {
        SDL_Event e{};
        e.type = m_wake_event;
        SDL_PushEvent(&e);
    }
}

// --- Bucle --------------------------------------------------------------------

int App::wait_timeout_ms() const {
    // -1 = esperar indefinidamente (CPU 0 %). Si hay algo con fecha (tooltip,
    // aviso que caduca, cursor de texto parpadeando), dormimos solo hasta entonces.
    const auto now = Clock::now();
    std::optional<Clock::time_point> next = m_settle_frame;
    for (const auto& t : m_toasts) {
        if (!next || t.expires < *next) {
            next = t.expires;
        }
    }
    int timeout = -1;
    if (next) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(*next - now).count();
        timeout = static_cast<int>(std::clamp<long long>(ms, 0, 60'000));
    }
    if (ImGui::GetIO().WantTextInput) {
        timeout = timeout < 0 ? caret_blink_ms : std::min(timeout, caret_blink_ms);
    }
    return timeout;
}

int App::run() {
    draw_frame();
    SDL_ShowWindow(m_window.get());

    int pending_frames = frames_after_event;
    while (m_running) {
        SDL_Event event;
        if (pending_frames <= 0) {
            const int timeout = wait_timeout_ms();
            const bool got =
                timeout < 0 ? SDL_WaitEvent(&event) : SDL_WaitEventTimeout(&event, timeout);
            if (got) {
                handle_event(event);
            } else if (m_settle_frame && Clock::now() >= *m_settle_frame) {
                m_settle_frame.reset();
            }
            pending_frames = got ? frames_after_event : 1;
        }
        while (SDL_PollEvent(&event)) {
            handle_event(event);
            pending_frames = frames_after_event;
        }
        if (process_background()) {
            pending_frames = std::max(pending_frames, frames_after_event);
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
    remember_current_document();
    return 0;
}

bool App::process_background() {
    bool any = false;
    while (auto task = m_tasks.try_pop()) {
        (*task)();
        any = true;
    }
    if (m_engine->drain() > 0) {
        any = true;
    }
    if (m_events.dispatch()) {
        any = true;
    }
    // Caducar avisos.
    const auto now = Clock::now();
    const auto before = m_toasts.size();
    std::erase_if(m_toasts, [now](const Toast& t) { return t.expires <= now; });
    return any || before != m_toasts.size();
}

void App::handle_event(const SDL_Event& event) {
    if (event.type == m_wake_event) {
        m_wake_pending = false;
        return; // process_background() recoge los resultados
    }
    ImGui_ImplSDL3_ProcessEvent(&event);
    switch (event.type) {
    case SDL_EVENT_QUIT:
        request_quit();
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (event.window.windowID == SDL_GetWindowID(m_window.get())) {
            request_quit();
        }
        break;
    case SDL_EVENT_DROP_FILE:
        if (event.drop.data != nullptr) {
            const std::string utf8{event.drop.data};
            const std::filesystem::path path = core::path_from_utf8(utf8);
            auto ext = path.extension().u8string();
            std::ranges::transform(ext, ext.begin(), [](char8_t ch) {
                return static_cast<char8_t>(std::tolower(ch));
            });
            if (ext == u8".pdf") {
                open_document(path);
            } else {
                notify("Solo se pueden abrir archivos PDF", true);
            }
        }
        break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
        const float scale = SDL_GetWindowDisplayScale(m_window.get());
        if (scale > 0.0f && scale != m_dpi_scale) {
            m_dpi_scale = scale;
            m_ctx->dpi_scale = scale;
            apply_theme();
        }
        break;
    }
    default:
        break;
    }
    // Entrada del usuario: programar un redibujo diferido para tooltips.
    if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
        event.type == SDL_EVENT_KEY_UP) {
        m_settle_frame = Clock::now() + settle_delay;
    }
}

void App::draw_frame() {
    // Cambios de estilo solo entre cuadros: nunca con colores apilados.
    if (m_theme_pending) {
        m_theme_pending = false;
        apply_theme();
        save_settings();
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    draw_ui();
    ImGui::Render();

    SDL_Renderer* renderer = m_renderer.get();
    const ImVec2 fb_scale = ImGui::GetIO().DisplayFramebufferScale;
    SDL_SetRenderScale(renderer, fb_scale.x, fb_scale.y);
    const ImVec4 bg = m_theme.colors().canvas;
    SDL_SetRenderDrawColorFloat(renderer, bg.x, bg.y, bg.z, 1.0f);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    if (!m_capture_path.empty()) {
        if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr); shot != nullptr) {
            const auto path = to_utf8(m_capture_path);
            SDL_SaveBMP(shot, path.c_str());
            SDL_DestroySurface(shot);
        }
        m_capture_path.clear();
    }
    SDL_RenderPresent(renderer);
}

void App::step(int frames) {
    for (int i = 0; i < frames && m_running; ++i) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            handle_event(event);
        }
        process_background();
        draw_frame();
    }
}

void App::settle() {
    // Cada cuadro puede encolar trabajo nuevo (p. ej. renders de páginas que
    // aparecen): repetimos esperar + aplicar unas cuantas veces.
    for (int round = 0; round < 8; ++round) {
        m_engine->wait_idle();
        step(2);
    }
}

bool App::capture(const std::filesystem::path& bmp) {
    m_capture_path = bmp;
    step(1);
    std::error_code ec;
    return std::filesystem::exists(bmp, ec);
}

// --- Interfaz -----------------------------------------------------------------

void App::draw_ui() {
    modules::AppContext& c = *m_ctx;
    const ui::Metrics& m = m_theme.metrics();
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    // Layout: barra superior, barra contextual (solo en modo edición), panel lateral y lienzo.
    const float toolbar_h = m.toolbar_height;
    const bool context_bar = c.doc.open && c.tool != modules::Tool::select_text;
    const float context_h = context_bar ? m.toolbar_height * 0.9f : 0.0f;
    const bool sidebar = c.doc.open && c.sidebar_visible;
    const float sidebar_w = sidebar ? m.sidebar_width : 0.0f;
    const float top = vp->WorkPos.y + toolbar_h + context_h;
    c.sidebar_pos = {vp->WorkPos.x, top};
    c.sidebar_size = {sidebar_w, vp->WorkSize.y - toolbar_h - context_h};
    c.canvas_pos = {vp->WorkPos.x + sidebar_w, top};
    c.canvas_size = {vp->WorkSize.x - sidebar_w, vp->WorkSize.y - toolbar_h - context_h};

    // Atajos globales de la app
    const ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, global)) {
        c.request_file(modules::FileDialog::open_pdf,
                       [this](std::filesystem::path p) { open_document(p); });
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_W, global) && c.doc.open) {
        guard_unsaved([this] {
            remember_current_document();
            m_engine->close();
            m_ctx->doc = {};
            ++m_ctx->doc.generation;
            m_commands.clear();
            m_ctx->doc.saved_command_revision = m_commands.revision();
            m_ctx->set_tool(modules::Tool::select_text);
            m_events.publish(modules::DocumentClosed{});
        });
    }
    if (!ImGui::GetIO().WantTextInput) {
        if ((ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, global)) && c.editable()) {
            m_commands.undo();
        }
        if ((ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, global) ||
             ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, global)) &&
            c.editable()) {
            m_commands.redo();
        }
    }
    if (ImGui::Shortcut(ImGuiKey_F1, global)) {
        m_show_help = !m_show_help;
    }
    for (auto& module : m_modules) {
        module->handle_shortcuts();
    }

    draw_toolbar();
    if (context_bar) {
        draw_context_bar();
    }
    // Los módulos dibujan sus paneles (el viewer, el lienzo; navigation, el panel lateral).
    for (auto& module : m_modules) {
        module->draw_ui();
    }
    draw_modals();
    draw_help();
    draw_toasts();
    update_title();
}

void App::draw_toolbar() {
    modules::AppContext& c = *m_ctx;
    const ui::Metrics& m = m_theme.metrics();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize({vp->WorkSize.x, m.toolbar_height});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, m_theme.colors().surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        {8.0f * m_dpi_scale, (m.toolbar_height - m.icon_button) * 0.5f});
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##toolbar", nullptr, flags);

    if (ui::icon_button("open", ui::icon::folder_open, "Abrir (Ctrl+O)", m_theme)) {
        c.request_file(modules::FileDialog::open_pdf,
                       [this](std::filesystem::path p) { open_document(p); });
    }
    ImGui::SameLine(0, 2);
    if (ui::icon_button("recent", ui::icon::history, "Recientes", m_theme,
                        !m_settings.recents.empty())) {
        ImGui::OpenPopup("##recents");
    }
    if (ImGui::BeginPopup("##recents")) {
        std::optional<std::filesystem::path> chosen;
        for (const auto& r : m_settings.recents) {
            const std::string label = to_utf8(r.path.filename()) + "##" + to_utf8(r.path);
            if (ImGui::MenuItem(label.c_str())) {
                chosen = r.path;
            }
            ui::tooltip(to_utf8(r.path).c_str());
        }
        ImGui::EndPopup();
        if (chosen) {
            open_document(*chosen);
        }
    }
    ImGui::SameLine(0, 2);
    find_module(m_modules, "export")->draw_toolbar();
    ui::toolbar_separator(m_theme);

    const std::string undo_tip = m_commands.can_undo()
                                     ? "Deshacer: " + m_commands.undo_description() + " (Ctrl+Z)"
                                     : std::string{"Deshacer (Ctrl+Z)"};
    if (ui::icon_button("undo", ui::icon::undo, undo_tip.c_str(), m_theme,
                        m_commands.can_undo() && c.editable())) {
        m_commands.undo();
    }
    ImGui::SameLine(0, 2);
    const std::string redo_tip = m_commands.can_redo()
                                     ? "Rehacer: " + m_commands.redo_description() + " (Ctrl+Y)"
                                     : std::string{"Rehacer (Ctrl+Y)"};
    if (ui::icon_button("redo", ui::icon::redo, redo_tip.c_str(), m_theme,
                        m_commands.can_redo() && c.editable())) {
        m_commands.redo();
    }
    ui::toolbar_separator(m_theme);
    find_module(m_modules, "navigation")->draw_toolbar();
    ui::toolbar_separator(m_theme);
    find_module(m_modules, "viewer")->draw_toolbar();
    ui::toolbar_separator(m_theme);
    find_module(m_modules, "text_edit")->draw_toolbar();
    ImGui::SameLine(0, 2);
    find_module(m_modules, "images")->draw_toolbar();
    ImGui::SameLine(0, 2);
    find_module(m_modules, "annotations")->draw_toolbar();
    ImGui::SameLine(0, 2);
    find_module(m_modules, "pages")->draw_toolbar();
    ui::toolbar_separator(m_theme);
    find_module(m_modules, "search")->draw_toolbar();

    // A la derecha: tema y ayuda.
    const float right = vp->WorkSize.x - 2 * m.icon_button - 14.0f * m_dpi_scale;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, right));
    const bool dark = m_settings.theme == core::ThemeMode::dark;
    if (ui::icon_button("theme", dark ? ui::icon::sun : ui::icon::moon,
                        dark ? "Tema claro" : "Tema oscuro", m_theme)) {
        m_settings.theme = dark ? core::ThemeMode::light : core::ThemeMode::dark;
        // NO aplicar aquí: estamos dentro del cuadro con colores apilados
        // (PushStyleColor) y el Pop restauraría los del tema anterior.
        m_theme_pending = true;
    }
    ImGui::SameLine(0, 2);
    if (ui::icon_button("help", ui::icon::keyboard, "Atajos de teclado (F1)", m_theme, true,
                        m_show_help)) {
        m_show_help = !m_show_help;
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void App::draw_context_bar() {
    const ui::Metrics& m = m_theme.metrics();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = m.toolbar_height * 0.9f;
    ImGui::SetNextWindowPos({vp->WorkPos.x, vp->WorkPos.y + m.toolbar_height});
    ImGui::SetNextWindowSize({vp->WorkSize.x, h});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, m_theme.colors().surface_alt);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        {12.0f * m_dpi_scale, (h - ImGui::GetFrameHeight()) * 0.5f});
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##context_bar", nullptr, flags);
    for (const auto& item : m_ctx->context_bar_items) {
        if (item()) {
            break; // una sola herramienta activa a la vez
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void App::draw_toasts() {
    if (m_toasts.empty()) {
        return;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->WorkPos.y + vp->WorkSize.y - 24.0f * m_dpi_scale;
    int i = 0;
    for (auto it = m_toasts.rbegin(); it != m_toasts.rend(); ++it, ++i) {
        const ImVec2 size = ImGui::CalcTextSize(it->text.c_str());
        const ImVec2 pad{14.0f * m_dpi_scale, 8.0f * m_dpi_scale};
        const ImVec2 box{size.x + pad.x * 2, size.y + pad.y * 2};
        y -= box.y;
        ImGui::SetNextWindowPos({vp->WorkPos.x + (vp->WorkSize.x - box.x) * 0.5f, y});
        ImGui::SetNextWindowBgAlpha(0.95f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
        ImGui::PushStyleColor(ImGuiCol_WindowBg,
                              it->error ? m_theme.colors().danger : m_theme.colors().surface_alt);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              it->error ? ImVec4{1, 1, 1, 1} : m_theme.colors().text);
        const std::string id = "##toast" + std::to_string(i);
        ImGui::Begin(id.c_str(), nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
        ImGui::TextUnformatted(it->text.c_str());
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        y -= 6.0f * m_dpi_scale;
    }
}

void App::draw_modals() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const float button_w = 130.0f * m_dpi_scale;

    if (m_confirm && !ImGui::IsPopupOpen("##confirm")) {
        ImGui::OpenPopup("##confirm");
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("##confirm", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        if (m_confirm) {
            ImGui::PushFont(m_fonts.semibold, 0.0f);
            ImGui::TextUnformatted(m_confirm->title.c_str());
            ImGui::PopFont();
            ImGui::TextUnformatted(m_confirm->message.c_str());
            ImGui::Separator();
            if (ImGui::Button("Aceptar", {button_w, 0}) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                auto action = std::move(m_confirm->on_yes);
                m_confirm.reset();
                ImGui::CloseCurrentPopup();
                if (action) {
                    action();
                }
            } else {
                ImGui::SameLine();
                if (ImGui::Button("Cancelar", {button_w, 0}) ||
                    ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    m_confirm.reset();
                    ImGui::CloseCurrentPopup();
                }
            }
        } else {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_unsaved_open && !ImGui::IsPopupOpen("##unsaved")) {
        ImGui::OpenPopup("##unsaved");
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("##unsaved", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushFont(m_fonts.semibold, 0.0f);
        ImGui::TextUnformatted("Hay cambios sin guardar");
        ImGui::PopFont();
        ImGui::Text("¿Guardar los cambios de «%s»?", to_utf8(m_ctx->doc.path.filename()).c_str());
        ImGui::Separator();
        if (ImGui::Button("Guardar", {button_w, 0})) {
            m_unsaved_open = false;
            ImGui::CloseCurrentPopup();
            auto action = std::move(m_unsaved_action);
            m_ctx->save_document([action = std::move(action)](bool ok) {
                if (ok && action) {
                    action();
                }
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("No guardar", {button_w, 0})) {
            m_unsaved_open = false;
            ImGui::CloseCurrentPopup();
            auto action = std::move(m_unsaved_action);
            m_ctx->doc.structure_dirty = false;
            m_ctx->doc.saved_command_revision = m_commands.revision(); // descartar
            if (action) {
                action();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", {button_w, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_unsaved_open = false;
            m_unsaved_action = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::draw_help() {
    if (!m_show_help) {
        return;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            {0.5f, 0.5f});
    ImGui::SetNextWindowSize({620.0f * m_dpi_scale, 0}, ImGuiCond_Appearing);
    if (ImGui::Begin("Atajos de teclado", &m_show_help,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        struct Row {
            const char* keys;
            const char* action;
        };
        constexpr Row rows[] = {
            {"Ctrl+O", "Abrir PDF"},
            {"Ctrl+S / Ctrl+Shift+S", "Guardar / Guardar como"},
            {"Ctrl+W", "Cerrar documento"},
            {"Ctrl+Z / Ctrl+Y", "Deshacer / Rehacer"},
            {"Ctrl+F, F3, Shift+F3", "Buscar, siguiente, anterior"},
            {"Ctrl+G", "Ir a página"},
            {"← / →, Inicio / Fin", "Página anterior/siguiente, primera/última"},
            {"RePág / AvPág, Espacio", "Desplazar una pantalla"},
            {"Ctrl+rueda, Ctrl++ / Ctrl+-", "Zoom"},
            {"Ctrl+0 / Ctrl+1 / Ctrl+2", "Ajustar ancho / 100 % / ajustar página"},
            {"Ctrl+C, Ctrl+A", "Copiar selección, seleccionar texto de la página"},
            {"E", "Editar objetos (mover, redimensionar, reemplazar)"},
            {"T", "Añadir texto"},
            {"Ctrl+Shift+I", "Insertar imagen"},
            {"Supr", "Eliminar objeto seleccionado"},
            {"Ctrl+H / Ctrl+U", "Resaltar / subrayar la selección"},
            {"N / D", "Nota / dibujo libre"},
            {"Ctrl+R / Ctrl+Shift+R", "Rotar página a la derecha / izquierda"},
            {"F4", "Mostrar u ocultar el panel lateral"},
            {"Esc", "Salir de la herramienta actual"},
        };
        if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Atajo", ImGuiTableColumnFlags_WidthFixed,
                                    200.0f * m_dpi_scale);
            ImGui::TableSetupColumn("Acción");
            for (const auto& r : rows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushFont(m_fonts.semibold, 0.0f);
                ImGui::TextUnformatted(r.keys);
                ImGui::PopFont();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r.action);
            }
            ImGui::EndTable();
        }
        ImGui::Separator();
        ImGui::TextDisabled("pdf-lite %s · 100 %% local, sin conexión a internet",
                            core::app_version);
    }
    ImGui::End();
}

void App::update_title() {
    std::string title = core::app_name;
    if (m_ctx->doc.open) {
        title = (m_ctx->dirty() ? "• " : "") + to_utf8(m_ctx->doc.path.filename()) + " — " + title;
    }
    if (title != m_title) {
        m_title = title;
        SDL_SetWindowTitle(m_window.get(), m_title.c_str());
    }
}

void App::notify(std::string text, bool error) {
    m_toasts.push_back({std::move(text), error, Clock::now() + toast_duration});
    while (m_toasts.size() > 3) {
        m_toasts.pop_front();
    }
}

// --- Documento ----------------------------------------------------------------

void App::guard_unsaved(std::function<void()> action) {
    if (m_ctx->dirty()) {
        m_unsaved_action = std::move(action);
        m_unsaved_open = true;
    } else {
        action();
    }
}

void App::open_document(const std::filesystem::path& path) {
    guard_unsaved([this, path] { do_open(path); });
}

void App::do_open(const std::filesystem::path& path) {
    remember_current_document();
    m_engine->open(path, [this, path](pdf::Result<pdf::DocInfo> r) {
        if (!r) {
            notify("No se pudo abrir «" + to_utf8(path.filename()) + "»: " + r.error(), true);
            return;
        }
        modules::AppContext& c = *m_ctx;
        c.doc.open = true;
        c.doc.path = r->path;
        c.doc.title = r->title;
        c.doc.pages = r->pages;
        c.doc.current_page = 0;
        ++c.doc.generation;
        c.doc.page_revision.assign(c.doc.pages.size(), 0);
        c.doc.structure_dirty = false;
        m_commands.clear();
        c.doc.saved_command_revision = m_commands.revision();
        c.selection = {};
        c.set_tool(modules::Tool::select_text);
        // Conservar la última página/zoom si ya era reciente.
        int last_page = 0;
        float zoom = 0.0f;
        if (const auto* recent = m_settings.find_recent(path)) {
            last_page = recent->last_page;
            zoom = recent->zoom;
        }
        // Los módulos leen settings en DocumentOpened (antes de que lo movamos al frente).
        m_events.publish(modules::DocumentOpened{});
        m_settings.touch_recent(path, last_page, zoom);
        save_settings();
    });
}

void App::remember_current_document() {
    if (m_ctx && m_ctx->doc.open) {
        m_settings.touch_recent(m_ctx->doc.path, m_ctx->doc.current_page,
                                m_ctx->fit_width ? 0.0f : m_ctx->zoom);
    }
}

void App::request_quit() {
    guard_unsaved([this] { m_running = false; });
}

void App::apply_theme() {
    m_theme.apply(m_settings.theme, m_dpi_scale);
}

void App::save_settings() {
    if (m_settings_path.empty()) {
        return;
    }
    m_settings.sidebar_visible = m_ctx ? m_ctx->sidebar_visible : m_settings.sidebar_visible;
    static_cast<void>(core::save_settings(m_settings, m_settings_path));
}

} // namespace app
