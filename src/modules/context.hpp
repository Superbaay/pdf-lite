#pragma once

// Contrato entre la app y los módulos. Los módulos solo se conocen a través de
// lo que hay aquí (estado compartido, bus de eventos, comandos y el Engine):
// nunca se incluyen entre sí.

#include "core/command_stack.hpp"
#include "core/event_bus.hpp"
#include "core/settings.hpp"
#include "pdf/engine.hpp"
#include "pdf/types.hpp"
#include "ui/fonts.hpp"
#include "ui/theme.hpp"

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Renderer;

namespace modules {

// --- Estado compartido ---------------------------------------------------------

enum class Tool : std::uint8_t {
    select_text, // por defecto: seleccionar y copiar texto
    edit_objects,
    add_text,
    highlight,
    underline,
    strikeout,
    note,
    ink,
};

struct DocumentState {
    bool open = false;
    std::filesystem::path path;
    std::string title;
    std::vector<pdf::PageSize> pages;
    int current_page = 0;
    // Sube cuando cambia la estructura (abrir, borrar/mover páginas, reabrir
    // tras guardar): toda caché indexada por página queda inválida.
    std::uint64_t generation = 0;
    // Revisión por página: sube cada vez que se edita esa página.
    std::vector<std::uint64_t> page_revision;
    std::size_t saved_command_revision = 0;
    bool structure_dirty = false; // cambios no deshacibles sin guardar
    int pending_edits = 0;        // operaciones de edición aún en el worker

    [[nodiscard]] int page_count() const { return static_cast<int>(pages.size()); }
};

// Selección de texto actual (una página).
struct TextSelectionState {
    int page = -1;
    std::vector<pdf::Rect> rects;
    std::string text;
    [[nodiscard]] bool empty() const { return page < 0 || rects.empty(); }
};

struct SearchState {
    std::vector<pdf::SearchHit> hits;
    int current = -1;
};

// Geometría de una página visible en este cuadro (la rellena el viewer).
struct PageLayout {
    int page = 0;
    ImVec2 origin;      // esquina superior izquierda en pantalla
    float scale = 1.0f; // píxeles de pantalla por punto PDF

    [[nodiscard]] ImVec2 to_screen(pdf::Point p) const {
        return {origin.x + p.x * scale, origin.y + p.y * scale};
    }
    [[nodiscard]] pdf::Point to_page(ImVec2 s) const {
        return {(s.x - origin.x) / scale, (s.y - origin.y) / scale};
    }
};

// Entrada de ratón sobre una página, para herramientas distintas de select_text.
struct PageInput {
    PageLayout layout;
    pdf::Point point; // coordenadas de vista (puntos)
    bool clicked = false;
    bool double_clicked = false;
    bool dragging = false;
    bool released = false;
    bool hovered = true;
};

// --- Eventos ---------------------------------------------------------------------

struct DocumentOpened {};
struct DocumentClosed {};
// La estructura cambió (generation subió): recargar índices, miniaturas, etc.
struct DocumentStructureChanged {};
struct PageEdited {
    int page;
};
struct GoToPage {
    int page;
};
// Desplaza la vista para que `rect` (coordenadas de vista) quede visible.
struct RevealRect {
    int page;
    pdf::Rect rect;
};
struct ToolChanged {
    Tool tool;
};
// Petición de operación sobre una página (la atiende el módulo pages).
struct PageOperation {
    enum class Kind : std::uint8_t { rotate_cw, rotate_ccw, move_up, move_down, remove, extract };
    Kind kind;
    int page;
};

// --- Contexto ------------------------------------------------------------------

enum class FileDialog : std::uint8_t { open_pdf, save_pdf, open_image, import_pdf };

struct AppContext {
    AppContext(pdf::Engine& engine_, core::EventBus& events_, core::CommandStack& commands_,
               core::Settings& settings_, ui::Theme& theme_, const ui::Fonts& fonts_,
               SDL_Renderer* renderer_, float dpi_scale_)
        : engine{engine_}, events{events_}, commands{commands_}, settings{settings_}, theme{theme_},
          fonts{fonts_}, renderer{renderer_}, dpi_scale{dpi_scale_} {}

    pdf::Engine& engine;
    core::EventBus& events;
    core::CommandStack& commands;
    core::Settings& settings;
    ui::Theme& theme;
    const ui::Fonts& fonts;
    SDL_Renderer* renderer = nullptr;
    float dpi_scale = 1.0f;

    DocumentState doc;
    TextSelectionState selection;
    SearchState search;
    Tool tool = Tool::select_text;
    std::vector<PageLayout> visible_pages; // del cuadro actual

    // Zonas de la ventana que calcula la app en cada cuadro.
    ImVec2 canvas_pos;
    ImVec2 canvas_size;
    ImVec2 sidebar_pos;
    ImVec2 sidebar_size;
    bool sidebar_visible = true;
    float zoom = 1.0f; // 1 = 100 % (tamaño físico real)
    bool fit_width = true;

    // Servicios que implementa la app:
    std::function<void(FileDialog, std::function<void(std::filesystem::path)>)> request_file;
    std::function<void(std::string message, bool error)> notify;
    std::function<void(std::string title, std::string message, std::function<void()> on_yes)>
        confirm;
    std::function<void(std::filesystem::path)> open_document;
    // Lo registra el módulo export: guarda (o "guardar como" si hace falta) y
    // llama a `done(true)` si terminó bien.
    std::function<void(std::function<void(bool)>)> save_document;

    // Ganchos que registran los módulos y usa el viewer:
    std::vector<std::function<void(ImDrawList&, const PageLayout&)>> page_overlays;
    std::vector<std::function<void(const PageInput&)>> page_input_handlers;
    // Barra contextual (solo visible si algún módulo dibuja algo).
    std::vector<std::function<bool()>> context_bar_items;
    // Menú contextual (clic derecho) sobre la página.
    std::vector<std::function<void(const PageInput&)>> page_context_menu;

    void set_tool(Tool t) {
        if (tool != t) {
            tool = t;
            events.publish(ToolChanged{t});
        }
    }
    [[nodiscard]] bool dirty() const {
        return doc.open &&
               (doc.structure_dirty || commands.revision() != doc.saved_command_revision);
    }
    [[nodiscard]] bool editable() const { return doc.open && doc.pending_edits == 0; }
    // Marca una página como editada: sube su revisión y avisa a los módulos.
    void page_edited(int page) {
        if (page >= 0 && page < doc.page_count()) {
            ++doc.page_revision[static_cast<std::size_t>(page)];
            events.publish(PageEdited{page});
        }
    }
};

// --- Interfaz de módulo ----------------------------------------------------------

class Module {
public:
    virtual ~Module() = default;
    [[nodiscard]] virtual std::string_view id() const = 0;
    virtual void init(AppContext& ctx) = 0;
    // Ventanas/paneles propios del módulo (se llama cada cuadro).
    virtual void draw_ui() = 0;
    // Botones en la barra superior (opcional).
    virtual void draw_toolbar() {}
    // Atajos de teclado globales (opcional).
    virtual void handle_shortcuts() {}
};

} // namespace modules
