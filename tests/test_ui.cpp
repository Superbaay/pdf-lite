// Pruebas de interfaz de punta a punta: la app REAL (módulos, Engine, PDFium,
// ImGui y SDL_Renderer) en una ventana oculta. La "mano" del usuario son
// eventos sintéticos de SDL (ratón, rueda, teclado, texto, soltar archivo):
// recorren exactamente el mismo camino que los eventos reales.

#define DOCTEST_CONFIG_IMPLEMENT
#include "app/app.hpp"
#include "core/utf.hpp"

#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED // main() propio: solo necesitamos SDL_SetMainReady
#include <SDL3/SDL_main.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path out_dir() {
    const fs::path dir = fs::current_path() / "test_output" / "ui";
    fs::create_directories(dir);
    return dir;
}

// --- Generador de PDFs de prueba (texto en cada página + índice) ------------

void write_sample_pdf(const fs::path& path, int pages, const std::string& title) {
    std::vector<std::string> objs;
    auto add = [&](std::string body) {
        objs.push_back(std::move(body));
        return static_cast<int>(objs.size());
    };
    const int font = add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                         "/Encoding /WinAnsiEncoding >>");
    const int pages_id = add("");
    std::vector<int> page_ids;
    for (int i = 0; i < pages; ++i) {
        std::string content =
            "BT /F1 28 Tf 72 740 Td (" + title + " - Pagina " + std::to_string(i + 1) + ") Tj ET\n";
        int y = 690;
        for (int j = 0; j < 25; ++j) {
            std::string line = (j == 5 && i % 7 == 0)
                                   ? "Palabra clave: ornitorrinco aparece aqui."
                                   : "Linea " + std::to_string(j + 1) +
                                         ": el veloz murcielago hindu comia feliz cardillo.";
            content += "BT /F1 11 Tf 72 " + std::to_string(y) + " Td (" + line + ") Tj ET\n";
            y -= 20;
        }
        const int stream = add("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" +
                               content + "\nendstream");
        page_ids.push_back(add("<< /Type /Page /Parent " + std::to_string(pages_id) +
                               " 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 " +
                               std::to_string(font) + " 0 R >> >> /Contents " +
                               std::to_string(stream) + " 0 R >>"));
    }
    std::string kids;
    for (int id : page_ids) {
        kids += std::to_string(id) + " 0 R ";
    }
    objs[static_cast<std::size_t>(pages_id - 1)] =
        "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages) + " >>";
    // Índice: un capítulo cada 5 páginas.
    const int outlines = add("");
    std::vector<int> chapters;
    for (int p = 0; p < pages; p += 5) {
        chapters.push_back(p);
    }
    const int first = static_cast<int>(objs.size()) + 1;
    for (std::size_t i = 0; i < chapters.size(); ++i) {
        const int id = first + static_cast<int>(i);
        std::string o = "<< /Title (Capitulo " + std::to_string(i + 1) + ") /Parent " +
                        std::to_string(outlines) + " 0 R /Dest [" +
                        std::to_string(page_ids[static_cast<std::size_t>(chapters[i])]) +
                        " 0 R /Fit]";
        if (i > 0) {
            o += " /Prev " + std::to_string(id - 1) + " 0 R";
        }
        if (i + 1 < chapters.size()) {
            o += " /Next " + std::to_string(id + 1) + " 0 R";
        }
        add(o + " >>");
    }
    objs[static_cast<std::size_t>(outlines - 1)] =
        "<< /Type /Outlines /First " + std::to_string(first) + " 0 R /Last " +
        std::to_string(first + static_cast<int>(chapters.size()) - 1) + " 0 R /Count " +
        std::to_string(chapters.size()) + " >>";
    const int catalog = add("<< /Type /Catalog /Pages " + std::to_string(pages_id) +
                            " 0 R /Outlines " + std::to_string(outlines) + " 0 R >>");

    std::string out = "%PDF-1.7\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objs.size(); ++i) {
        offsets.push_back(out.size());
        out += std::to_string(i + 1) + " 0 obj\n" + objs[i] + "\nendobj\n";
    }
    const std::size_t xref = out.size();
    out += "xref\n0 " + std::to_string(objs.size() + 1) + "\n0000000000 65535 f \n";
    for (std::size_t off : offsets) {
        char line[32];
        std::snprintf(line, sizeof(line), "%010zu 00000 n \n", off);
        out += line;
    }
    out += "trailer\n<< /Size " + std::to_string(objs.size() + 1) + " /Root " +
           std::to_string(catalog) + " 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    std::ofstream{path, std::ios::binary} << out;
}

// --- "Mano" sintética ---------------------------------------------------------

class Driver {
public:
    explicit Driver(const std::string& name) {
        const auto settings = out_dir() / (name + "-settings.json");
        fs::remove(settings);
        app::App::Options options;
        options.hidden = true;
        options.settings_path = settings;
        char arg0[] = "pdf_lite";
        char* argv[] = {arg0, nullptr};
        auto created = app::App::create(1, argv, options);
        REQUIRE_MESSAGE(created.has_value(), (created ? "" : created.error()));
        m_app = std::move(*created);
        m_app->step(2);
        // Diálogos de archivo: en vez de abrir uno nativo, devolvemos la ruta preparada.
        ctx().request_file = [this](modules::FileDialog, std::function<void(fs::path)> cb) {
            if (!m_next_file.empty()) {
                cb(std::exchange(m_next_file, {}));
            }
        };
    }

    app::App& app() { return *m_app; }
    modules::AppContext& ctx() { return m_app->context(); }
    void next_file(fs::path p) { m_next_file = std::move(p); }

    void push(SDL_Event e) { SDL_PushEvent(&e); }
    Uint32 window_id() const { return SDL_GetWindowID(m_app->window()); }

    void move(float x, float y) {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = window_id();
        e.motion.x = x;
        e.motion.y = y;
        push(e);
        m_app->step(1);
    }
    void button(float x, float y, bool down, Uint8 which = SDL_BUTTON_LEFT, Uint8 clicks = 1) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID = window_id();
        e.button.button = which;
        e.button.down = down;
        e.button.clicks = clicks;
        e.button.x = x;
        e.button.y = y;
        push(e);
        m_app->step(1);
    }
    void click(ImVec2 p, Uint8 which = SDL_BUTTON_LEFT) {
        move(p.x, p.y);
        button(p.x, p.y, true, which);
        button(p.x, p.y, false, which);
        m_app->step(1);
    }
    void double_click(ImVec2 p) {
        move(p.x, p.y);
        button(p.x, p.y, true, SDL_BUTTON_LEFT, 1);
        button(p.x, p.y, false, SDL_BUTTON_LEFT, 1);
        button(p.x, p.y, true, SDL_BUTTON_LEFT, 2);
        button(p.x, p.y, false, SDL_BUTTON_LEFT, 2);
        m_app->step(1);
    }
    void drag(ImVec2 from, ImVec2 to, int steps = 10) {
        move(from.x, from.y);
        button(from.x, from.y, true);
        for (int i = 1; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            move(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
            m_app->settle(); // deja responder al worker (p. ej. selección de texto)
        }
        button(to.x, to.y, false);
        m_app->settle();
    }
    void wheel(ImVec2 at, float dy, SDL_Keymod mod = SDL_KMOD_NONE) {
        if (mod != SDL_KMOD_NONE) {
            key_event(SDLK_LCTRL, SDL_SCANCODE_LCTRL, mod, true);
        }
        move(at.x, at.y);
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.windowID = window_id();
        e.wheel.y = dy;
        e.wheel.mouse_x = at.x;
        e.wheel.mouse_y = at.y;
        push(e);
        m_app->step(2);
        if (mod != SDL_KMOD_NONE) {
            key_event(SDLK_LCTRL, SDL_SCANCODE_LCTRL, SDL_KMOD_NONE, false);
        }
    }
    void key_event(SDL_Keycode key, SDL_Scancode sc, SDL_Keymod mod, bool down) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.windowID = window_id();
        e.key.key = key;
        e.key.scancode = sc;
        e.key.mod = mod;
        e.key.down = down;
        push(e);
        m_app->step(1);
    }
    // Pulsa una tecla con modificadores (p. ej. Ctrl+F).
    void key(SDL_Keycode key, SDL_Scancode sc, SDL_Keymod mod = SDL_KMOD_NONE) {
        if ((mod & SDL_KMOD_CTRL) != 0) {
            key_event(SDLK_LCTRL, SDL_SCANCODE_LCTRL, SDL_KMOD_LCTRL, true);
        }
        if ((mod & SDL_KMOD_SHIFT) != 0) {
            key_event(SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, mod, true);
        }
        key_event(key, sc, mod, true);
        key_event(key, sc, mod, false);
        if ((mod & SDL_KMOD_SHIFT) != 0) {
            key_event(SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, mod & ~SDL_KMOD_SHIFT, false);
        }
        if ((mod & SDL_KMOD_CTRL) != 0) {
            key_event(SDLK_LCTRL, SDL_SCANCODE_LCTRL, SDL_KMOD_NONE, false);
        }
        m_app->step(2);
    }
    void type(const char* text) {
        SDL_Event e{};
        e.type = SDL_EVENT_TEXT_INPUT;
        e.text.windowID = window_id();
        e.text.text = text; // el literal vive más que el paso del evento
        push(e);
        m_app->step(2);
    }
    void drop_file(const fs::path& path) {
        const auto u8 = path.u8string();
        m_drop = std::string{u8.begin(), u8.end()};
        SDL_Event e{};
        e.type = SDL_EVENT_DROP_FILE;
        e.drop.windowID = window_id();
        e.drop.data = m_drop.c_str();
        push(e);
        m_app->step(1);
    }

    void open(const fs::path& path) {
        ctx().open_document(path);
        m_app->settle();
        REQUIRE(ctx().doc.open);
    }

    // Punto en pantalla de una posición de página (puntos PDF desde arriba-izquierda).
    ImVec2 on_page(int page, float x_pt, float y_pt) {
        m_app->step(1);
        for (const auto& l : ctx().visible_pages) {
            if (l.page == page) {
                const ImVec2 p = l.to_screen({x_pt, y_pt});
                const auto& c = ctx();
                // El punto debe caer dentro del lienzo visible o el "clic" no haría nada.
                REQUIRE(p.x >= c.canvas_pos.x);
                REQUIRE(p.y >= c.canvas_pos.y);
                REQUIRE(p.x < c.canvas_pos.x + c.canvas_size.x);
                REQUIRE(p.y < c.canvas_pos.y + c.canvas_size.y);
                return p;
            }
        }
        FAIL("la página " << page << " no está visible");
        return {};
    }
    ImVec2 canvas_center() {
        return {ctx().canvas_pos.x + ctx().canvas_size.x * 0.5f,
                ctx().canvas_pos.y + ctx().canvas_size.y * 0.5f};
    }

    std::vector<pdf::ObjectInfo> objects(int page) {
        std::optional<std::vector<pdf::ObjectInfo>> out;
        ctx().engine.objects(page, [&](pdf::Result<std::vector<pdf::ObjectInfo>> r) {
            out = r ? std::move(*r) : std::vector<pdf::ObjectInfo>{};
        });
        m_app->settle();
        REQUIRE(out.has_value());
        return *out;
    }
    std::vector<pdf::SearchHit> search(const std::string& q) {
        std::optional<std::vector<pdf::SearchHit>> out;
        ctx().engine.search(q, false, false,
                            [&](std::vector<pdf::SearchHit> h) { out = std::move(h); });
        m_app->settle();
        REQUIRE(out.has_value());
        return *out;
    }

    std::string page_text(int page) {
        std::optional<std::string> out;
        ctx().engine.text_range(page, 0, 40, [&](pdf::Result<pdf::TextSelection> r) {
            out = r ? r->text : std::string{};
        });
        m_app->settle();
        REQUIRE(out.has_value());
        return *out;
    }

    // Comprueba que la captura no sea un lienzo vacío: debe haber píxeles
    // oscuros (texto) dentro del área de la primera página visible.
    bool page_has_ink(const std::string& name) {
        const auto path = out_dir() / (name + ".bmp");
        REQUIRE(m_app->capture(path));
        SDL_Surface* raw = SDL_LoadBMP(core::path_to_utf8(path).c_str());
        REQUIRE(raw != nullptr);
        SDL_Surface* s = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(raw);
        REQUIRE(s != nullptr);
        REQUIRE_FALSE(ctx().visible_pages.empty());
        const auto& l = ctx().visible_pages.front();
        const auto& size = ctx().doc.pages[static_cast<std::size_t>(l.page)];
        int dark = 0;
        const auto* px = static_cast<const Uint8*>(s->pixels);
        const int x0 = std::max(0, static_cast<int>(l.origin.x));
        const int y0 = std::max(0, static_cast<int>(l.origin.y));
        const int x1 = std::min(s->w, static_cast<int>(l.origin.x + size.width * l.scale));
        const int y1 = std::min(s->h, static_cast<int>(l.origin.y + size.height * l.scale));
        for (int y = y0; y < y1; y += 2) {
            for (int x = x0; x < x1; x += 2) {
                const Uint8* p = px + static_cast<std::ptrdiff_t>(y) * s->pitch + x * 4;
                if (p[0] < 80 && p[1] < 80 && p[2] < 80) {
                    ++dark;
                }
            }
        }
        SDL_DestroySurface(s);
        return dark > 50;
    }

private:
    std::unique_ptr<app::App> m_app;
    fs::path m_next_file;
    std::string m_drop;
};

fs::path sample(const std::string& name, int pages) {
    const auto path = out_dir() / name;
    if (!fs::exists(path)) {
        write_sample_pdf(path, pages, name.substr(0, name.find('.')));
    }
    return path;
}

} // namespace

// ============================================================================

TEST_CASE("Arranque: pantalla de bienvenida sin documento") {
    Driver d{"bienvenida"};
    CHECK_FALSE(d.ctx().doc.open);
    REQUIRE(d.app().capture(out_dir() / "bienvenida.bmp"));
    CHECK(d.app().running());
}

TEST_CASE("Visor: abrir, renderizar, navegar y zoom") {
    Driver d{"visor"};
    d.open(sample("corto.pdf", 10));
    CHECK(d.ctx().doc.page_count() == 10);
    CHECK_FALSE(d.ctx().visible_pages.empty());
    CHECK(d.page_has_ink("visor-pagina1"));

    SUBCASE("teclado: Fin, Inicio, flechas") {
        d.key(SDLK_END, SDL_SCANCODE_END);
        d.app().settle();
        CHECK(d.ctx().doc.current_page == 9);
        d.key(SDLK_HOME, SDL_SCANCODE_HOME);
        d.app().settle();
        CHECK(d.ctx().doc.current_page == 0);
        d.key(SDLK_RIGHT, SDL_SCANCODE_RIGHT);
        d.app().settle();
        CHECK(d.ctx().doc.current_page == 1);
    }
    SUBCASE("Ctrl+G y escribir un número de página") {
        d.key(SDLK_G, SDL_SCANCODE_G, SDL_KMOD_LCTRL);
        d.key(SDLK_A, SDL_SCANCODE_A, SDL_KMOD_LCTRL); // seleccionar el contenido del campo
        d.type("7");
        d.key(SDLK_RETURN, SDL_SCANCODE_RETURN);
        d.app().settle();
        CHECK(d.ctx().doc.current_page == 6);
    }
    SUBCASE("rueda desplaza; Ctrl+rueda hace zoom; Ctrl+1 y Ctrl+0") {
        d.wheel(d.canvas_center(), -40.0f);
        d.app().settle();
        CHECK(d.ctx().doc.current_page >= 1);
        const float before = d.ctx().zoom;
        d.wheel(d.canvas_center(), 2.0f, SDL_KMOD_LCTRL);
        d.app().settle();
        CHECK(d.ctx().zoom > before);
        CHECK_FALSE(d.ctx().fit_width);
        d.key(SDLK_1, SDL_SCANCODE_1, SDL_KMOD_LCTRL);
        d.app().settle();
        CHECK(d.ctx().zoom == doctest::Approx(1.0f));
        d.key(SDLK_0, SDL_SCANCODE_0, SDL_KMOD_LCTRL);
        d.app().settle();
        CHECK(d.ctx().fit_width);
        CHECK(d.page_has_ink("visor-zoom"));
    }
    SUBCASE("panel lateral: F4 lo oculta y lo muestra") {
        CHECK(d.ctx().sidebar_visible);
        d.key(SDLK_F4, SDL_SCANCODE_F4);
        CHECK_FALSE(d.ctx().sidebar_visible);
        d.key(SDLK_F4, SDL_SCANCODE_F4);
        CHECK(d.ctx().sidebar_visible);
    }
}

TEST_CASE("Navegación: búsqueda con Ctrl+F, selección y copia") {
    Driver d{"busqueda"};
    d.open(sample("corto.pdf", 10));

    d.key(SDLK_F, SDL_SCANCODE_F, SDL_KMOD_LCTRL);
    d.type("ornitorrinco");
    d.key(SDLK_RETURN, SDL_SCANCODE_RETURN);
    d.app().settle();
    REQUIRE(d.ctx().search.hits.size() == 2); // páginas 1 y 8
    CHECK(d.ctx().search.current == 0);
    d.key(SDLK_F3, SDL_SCANCODE_F3);
    d.app().settle();
    CHECK(d.ctx().search.current == 1);
    CHECK(d.ctx().doc.current_page == 7);
    REQUIRE(d.app().capture(out_dir() / "busqueda.bmp"));

    // Selección: arrastrar sobre el título de la página 1.
    // Esc sale del campo de búsqueda (que conserva el foco para seguir con Enter).
    d.key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    CHECK(d.ctx().search.hits.empty());
    d.key(SDLK_HOME, SDL_SCANCODE_HOME);
    d.app().settle();
    CHECK(d.ctx().doc.current_page == 0);
    d.drag(d.on_page(0, 70.0f, 40.0f), d.on_page(0, 330.0f, 40.0f));
    REQUIRE_FALSE(d.ctx().selection.empty());
    CHECK(d.ctx().selection.text.find("corto") != std::string::npos);
    d.key(SDLK_C, SDL_SCANCODE_C, SDL_KMOD_LCTRL);
    char* clip = SDL_GetClipboardText();
    REQUIRE(clip != nullptr);
    CHECK(std::string{clip} == d.ctx().selection.text);
    SDL_free(clip);
}

TEST_CASE("Índice del PDF y documento largo") {
    Driver d{"largo"};
    d.open(sample("largo.pdf", 320));
    CHECK(d.ctx().doc.page_count() == 320);
    d.key(SDLK_END, SDL_SCANCODE_END);
    d.app().settle();
    CHECK(d.ctx().doc.current_page == 319);
    CHECK(d.page_has_ink("largo-final"));
    // Recorrer el documento rápido no debe romper nada (cancelaciones de render).
    for (int i = 0; i < 30; ++i) {
        d.key(SDLK_PAGEUP, SDL_SCANCODE_PAGEUP);
    }
    d.app().settle();
    CHECK(d.ctx().doc.current_page < 319);
    CHECK(d.page_has_ink("largo-medio"));
}

TEST_CASE("Edición nivel 1: insertar texto, mover, deshacer y rehacer") {
    Driver d{"edicion"};
    d.open(sample("corto.pdf", 10));
    const auto before = d.objects(0).size();

    d.key(SDLK_T, SDL_SCANCODE_T);
    CHECK(d.ctx().tool == modules::Tool::add_text);
    d.click(d.on_page(0, 300.0f, 300.0f));
    d.type("¡Hola, señoría! ¿Qué tal?");
    d.key(SDLK_RETURN, SDL_SCANCODE_RETURN);
    d.app().settle();
    auto objs = d.objects(0);
    REQUIRE(objs.size() == before + 1);
    const auto inserted = std::ranges::find_if(
        objs, [](const pdf::ObjectInfo& o) { return o.text == "¡Hola, señoría! ¿Qué tal?"; });
    REQUIRE(inserted != objs.end());
    CHECK(inserted->bounds.x0 == doctest::Approx(300.0f).epsilon(0.02));
    CHECK(inserted->bounds.y0 == doctest::Approx(300.0f).epsilon(0.02));
    CHECK(d.ctx().dirty());
    CHECK(d.ctx().tool == modules::Tool::edit_objects);

    // Mover arrastrando el objeto insertado (ya en modo edición).
    const pdf::Rect b = inserted->bounds;
    const float cx = (b.x0 + b.x1) * 0.5f;
    const float cy = (b.y0 + b.y1) * 0.5f;
    d.drag(d.on_page(0, cx, cy), d.on_page(0, cx - 100.0f, cy + 40.0f));
    auto moved = d.objects(0);
    const auto m =
        std::ranges::find_if(moved, [&](const pdf::ObjectInfo& o) { return o.id == inserted->id; });
    REQUIRE(m != moved.end());
    CHECK(m->bounds.x0 == doctest::Approx(b.x0 - 100.0f).epsilon(0.02));
    CHECK(m->bounds.y0 == doctest::Approx(b.y0 + 40.0f).epsilon(0.02));

    // Deshacer el movimiento y la inserción; rehacer ambos.
    d.key(SDLK_Z, SDL_SCANCODE_Z, SDL_KMOD_LCTRL);
    d.app().settle();
    d.key(SDLK_Z, SDL_SCANCODE_Z, SDL_KMOD_LCTRL);
    d.app().settle();
    CHECK(d.objects(0).size() == before);
    d.key(SDLK_Y, SDL_SCANCODE_Y, SDL_KMOD_LCTRL);
    d.app().settle();
    d.key(SDLK_Y, SDL_SCANCODE_Y, SDL_KMOD_LCTRL);
    d.app().settle();
    auto redone = d.objects(0);
    CHECK(redone.size() == before + 1);
    REQUIRE(d.page_has_ink("edicion-texto"));

    // Borrar con Supr el objeto seleccionado.
    const auto again = std::ranges::find_if(
        redone, [&](const pdf::ObjectInfo& o) { return o.id == inserted->id; });
    REQUIRE(again != redone.end());
    d.click(d.on_page(0, (again->bounds.x0 + again->bounds.x1) / 2,
                      (again->bounds.y0 + again->bounds.y1) / 2));
    d.key(SDLK_DELETE, SDL_SCANCODE_DELETE);
    d.app().settle();
    CHECK(d.objects(0).size() == before);
}

TEST_CASE("Edición nivel 2: reemplazar texto existente y guardar copia completa") {
    Driver d{"reemplazo"};
    const auto src = out_dir() / "reemplazo-origen.pdf";
    fs::copy_file(sample("corto.pdf", 10), src, fs::copy_options::overwrite_existing);
    d.open(src);

    d.key(SDLK_E, SDL_SCANCODE_E);
    CHECK(d.ctx().tool == modules::Tool::edit_objects);
    // Doble clic sobre el título: selecciona y enfoca el campo de texto.
    d.double_click(d.on_page(0, 120.0f, 40.0f));
    d.app().settle();
    d.key(SDLK_A, SDL_SCANCODE_A, SDL_KMOD_LCTRL);
    d.type("Título reemplazado");
    d.key(SDLK_RETURN, SDL_SCANCODE_RETURN);
    d.app().settle();
    CHECK(d.search("reemplazado").size() == 1);
    CHECK(d.page_text(0).find("Título reemplazado") != std::string::npos);
    CHECK(d.page_text(0).find("corto - Pagina 1") == std::string::npos);

    // Guardar (sobrescribe el archivo abierto: se reabre) y verificar en disco.
    d.key(SDLK_S, SDL_SCANCODE_S, SDL_KMOD_LCTRL);
    d.app().settle();
    CHECK_FALSE(d.ctx().dirty());
    // Reabrir desde disco (una sola App por proceso: ImGui y PDFium son globales).
    d.open(src);
    CHECK(d.search("reemplazado").size() == 1);
    CHECK(d.page_text(0).find("corto - Pagina 1") == std::string::npos);
    CHECK(d.search("corto - Pagina 10").size() == 1); // el resto intacto
}

TEST_CASE("Anotaciones: resaltar selección y deshacer") {
    Driver d{"anotaciones"};
    d.open(sample("corto.pdf", 10));
    d.drag(d.on_page(0, 70.0f, 100.0f), d.on_page(0, 400.0f, 100.0f));
    REQUIRE_FALSE(d.ctx().selection.empty());
    d.key(SDLK_H, SDL_SCANCODE_H, SDL_KMOD_LCTRL);
    d.app().settle();
    CHECK(d.ctx().commands.undo_description() == "resaltar");
    CHECK(d.ctx().dirty());
    REQUIRE(d.app().capture(out_dir() / "resaltado.bmp"));
    d.key(SDLK_Z, SDL_SCANCODE_Z, SDL_KMOD_LCTRL);
    d.app().settle();
    CHECK_FALSE(d.ctx().commands.can_undo());

    // Dibujo libre
    d.key(SDLK_D, SDL_SCANCODE_D);
    CHECK(d.ctx().tool == modules::Tool::ink);
    d.drag(d.on_page(0, 100.0f, 250.0f), d.on_page(0, 300.0f, 350.0f));
    CHECK(d.ctx().commands.undo_description() == "dibujar");
}

TEST_CASE("Páginas: rotar, eliminar, unir y dividir") {
    Driver d{"paginas"};
    const auto src = out_dir() / "paginas-origen.pdf";
    fs::copy_file(sample("corto.pdf", 10), src, fs::copy_options::overwrite_existing);
    d.open(src);

    d.key(SDLK_R, SDL_SCANCODE_R, SDL_KMOD_LCTRL);
    d.app().settle();
    CHECK(d.ctx().doc.pages[0].width == doctest::Approx(792.0f));
    d.key(SDLK_Z, SDL_SCANCODE_Z, SDL_KMOD_LCTRL);
    d.app().settle();
    CHECK(d.ctx().doc.pages[0].width == doctest::Approx(612.0f));

    d.ctx().events.publish(modules::PageOperation{modules::PageOperation::Kind::remove, 3});
    d.app().step(2);
    CHECK(d.app().modal_open());
    d.key(SDLK_RETURN, SDL_SCANCODE_RETURN); // Aceptar
    d.app().settle();
    CHECK(d.ctx().doc.page_count() == 9);

    // Mover la primera página abajo: ahora la primera es la "Pagina 2".
    d.ctx().events.publish(modules::PageOperation{modules::PageOperation::Kind::move_down, 0});
    d.app().settle();
    CHECK(d.page_text(0).find("Pagina 2") != std::string::npos);
    CHECK(d.page_text(1).find("Pagina 1") != std::string::npos);

    const auto extracted = out_dir() / "extraida.pdf";
    fs::remove(extracted);
    d.next_file(extracted);
    d.ctx().events.publish(modules::PageOperation{modules::PageOperation::Kind::extract, 0});
    d.app().settle();
    CHECK(fs::exists(extracted));

    // Guardar como en otro archivo y comprobar el número de páginas.
    const auto saved = out_dir() / "paginas-guardado.pdf";
    fs::remove(saved);
    d.next_file(saved);
    d.key(SDLK_S, SDL_SCANCODE_S, static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT));
    d.app().settle();
    REQUIRE(fs::exists(saved));
    d.open(saved);
    CHECK(d.ctx().doc.page_count() == 9);
    d.open(extracted);
    CHECK(d.ctx().doc.page_count() == 1);
}

TEST_CASE("Cambios sin guardar: abrir otro archivo pregunta antes") {
    Driver d{"sin-guardar"};
    d.open(sample("corto.pdf", 10));
    d.key(SDLK_R, SDL_SCANCODE_R, SDL_KMOD_LCTRL);
    d.app().settle();
    REQUIRE(d.ctx().dirty());
    d.drop_file(sample("largo.pdf", 320));
    d.app().step(2);
    CHECK(d.app().modal_open());
    d.key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE); // Cancelar
    d.app().settle();
    CHECK_FALSE(d.app().modal_open());
    CHECK(d.ctx().doc.page_count() == 10);
}

TEST_CASE("Arrastrar y soltar abre el PDF; archivos dañados dan error sin cerrar la app") {
    Driver d{"soltar"};
    d.drop_file(sample("corto.pdf", 10));
    d.app().settle();
    CHECK(d.ctx().doc.page_count() == 10);

    const auto broken = out_dir() / "roto.pdf";
    std::ofstream{broken, std::ios::binary} << "%PDF-1.7 esto no es un pdf de verdad";
    d.drop_file(broken);
    d.app().settle();
    CHECK(d.app().running());
    CHECK(d.ctx().doc.page_count() == 10); // sigue el anterior abierto
}

TEST_CASE("Imágenes: insertar desde archivo y moverla") {
    Driver d{"imagenes"};
    d.open(sample("corto.pdf", 10));
    // Imagen de prueba: 64x32 roja, escrita como BMP con el propio SDL.
    const auto image = out_dir() / "rojo.bmp";
    SDL_Surface* surface = SDL_CreateSurface(64, 32, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surface != nullptr);
    SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGBA(surface, 255, 0, 0, 255));
    REQUIRE(SDL_SaveBMP(surface, core::path_to_utf8(image).c_str()));
    SDL_DestroySurface(surface);

    const auto before = d.objects(0).size();
    d.next_file(image);
    d.key(SDLK_I, SDL_SCANCODE_I, static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT));
    d.app().settle();
    auto objs = d.objects(0);
    REQUIRE(objs.size() == before + 1);
    const auto img = std::ranges::find_if(
        objs, [](const pdf::ObjectInfo& o) { return o.kind == pdf::ObjectKind::image; });
    REQUIRE(img != objs.end());
    CHECK(img->bounds.width() == doctest::Approx(48.0f).epsilon(0.02)); // 64 px a 96 ppp
    CHECK(d.ctx().tool == modules::Tool::edit_objects);
    REQUIRE(d.app().capture(out_dir() / "imagen.bmp"));
    d.key(SDLK_Z, SDL_SCANCODE_Z, SDL_KMOD_LCTRL);
    d.app().settle();
    CHECK(d.objects(0).size() == before);
}

TEST_CASE("Índice: el PDF de prueba trae un capítulo cada 5 páginas") {
    Driver d{"indice"};
    d.open(sample("corto.pdf", 10));
    std::optional<std::vector<pdf::OutlineItem>> items;
    d.ctx().engine.outline([&](std::vector<pdf::OutlineItem> v) { items = std::move(v); });
    d.app().settle();
    REQUIRE(items.has_value());
    REQUIRE(items->size() == 2);
    CHECK(items->at(0).title == "Capitulo 1");
    CHECK(items->at(1).page == 5);
    d.ctx().events.publish(modules::GoToPage{items->at(1).page});
    d.app().settle();
    CHECK(d.ctx().doc.current_page == 5);
}

TEST_CASE("Tema: clic en el botón alterna claro/oscuro y se guarda") {
    Driver d{"tema"};
    d.open(sample("corto.pdf", 10));
    const auto& c = d.ctx();
    const auto& m = c.theme.metrics();
    // Posición del botón de tema (ver App::draw_toolbar): pegado a la derecha.
    const float vp_w = ImGui::GetMainViewport()->WorkSize.x;
    const float right = vp_w - 2 * m.icon_button - 14.0f * c.dpi_scale;
    const ImVec2 button{right + m.icon_button * 0.5f, m.toolbar_height * 0.5f};
    CHECK(c.settings.theme == core::ThemeMode::dark);
    d.click(button);
    CHECK(c.settings.theme == core::ThemeMode::light);
    REQUIRE(d.app().capture(out_dir() / "tema-claro.bmp"));
    d.key(SDLK_F1, SDL_SCANCODE_F1); // ayuda de atajos
    REQUIRE(d.app().capture(out_dir() / "ayuda.bmp"));
}

int main(int argc, char** argv) {
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    const int result = context.run();
    SDL_Quit();
    return result;
}
