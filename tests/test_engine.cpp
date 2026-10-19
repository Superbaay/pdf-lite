// Pruebas de integración del Engine: PDFium real en su hilo worker.
// Los PDFs se generan en build/<preset>/tests/test_output/ (dentro del repo).

#include "pdf/engine.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <optional>

namespace {

namespace fs = std::filesystem;

fs::path out_dir() {
    const fs::path dir = fs::current_path() / "test_output";
    fs::create_directories(dir);
    return dir;
}

fs::path font_path() {
    return fs::path{PDFLITE_ASSETS_DIR} / "fonts" / "Inter-Regular.ttf";
}

// Convierte una llamada asíncrona en síncrona: lanza, espera al worker y
// ejecuta los callbacks en este hilo (que hace de "hilo de UI").
template <typename T, typename Start>
T sync(pdf::Engine& engine, Start start) {
    std::optional<T> out;
    start([&out](T v) { out.emplace(std::move(v)); });
    engine.wait_idle();
    engine.drain();
    REQUIRE(out.has_value());
    return std::move(*out);
}

// Fixture: un PDF de 3 páginas A4 con texto en las dos primeras.
fs::path make_sample(pdf::Engine& e, const std::string& name) {
    const auto path = out_dir() / name;
    auto created = sync<pdf::Result<void>>(
        e, [&](auto cb) { e.create_blank(path, 3, 595.0f, 842.0f, std::move(cb)); });
    REQUIRE(created.has_value());
    auto info = sync<pdf::Result<pdf::DocInfo>>(e, [&](auto cb) { e.open(path, std::move(cb)); });
    REQUIRE(info.has_value());

    pdf::TextSpec hello;
    hello.text = "Hola mundo";
    hello.size = 24.0f;
    hello.origin = {72.0f, 72.0f};
    REQUIRE(sync<pdf::Result<pdf::ObjectInfo>>(e, [&](auto cb) {
                e.insert_text(0, hello, std::move(cb));
            }).has_value());
    pdf::TextSpec second = hello;
    second.text = "Segunda página: mundo";
    REQUIRE(sync<pdf::Result<pdf::ObjectInfo>>(e, [&](auto cb) {
                e.insert_text(1, second, std::move(cb));
            }).has_value());
    auto saved =
        sync<pdf::Result<pdf::SaveOutcome>>(e, [&](auto cb) { e.save(path, std::move(cb)); });
    REQUIRE(saved.has_value());
    CHECK(saved->reopened);
    return path;
}

bool is_white(const pdf::Bitmap& bmp) {
    return std::ranges::all_of(bmp.pixels, [](std::uint8_t v) { return v == 0xFF; });
}

} // namespace

TEST_CASE("Engine: abrir, renderizar y errores") {
    pdf::Engine engine{font_path(), nullptr};

    SUBCASE("un archivo inexistente devuelve un error") {
        auto r = sync<pdf::Result<pdf::DocInfo>>(
            engine, [&](auto cb) { engine.open(out_dir() / "no-existe.pdf", std::move(cb)); });
        CHECK_FALSE(r.has_value());
    }

    SUBCASE("render a escala 2 produce el tamaño esperado y no está en blanco") {
        make_sample(engine, "render.pdf");
        auto r = sync<pdf::Result<pdf::RenderedPage>>(engine, [&](auto cb) {
            engine.render(0, 2.0f, pdf::RenderChannel::view, std::move(cb));
        });
        REQUIRE(r.has_value());
        CHECK(r->bitmap.width == 1190);
        CHECK(r->bitmap.height == 1684);
        CHECK(r->bitmap.pixels.size() == 1190u * 1684u * 4u);
        CHECK_FALSE(is_white(r->bitmap));

        auto blank = sync<pdf::Result<pdf::RenderedPage>>(engine, [&](auto cb) {
            engine.render(2, 0.5f, pdf::RenderChannel::view, std::move(cb));
        });
        REQUIRE(blank.has_value());
        CHECK(is_white(blank->bitmap));
    }

    SUBCASE("cancel_renders descarta lo pendiente") {
        make_sample(engine, "cancel.pdf");
        std::optional<pdf::Result<pdf::RenderedPage>> out;
        engine.render(0, 1.0f, pdf::RenderChannel::view, [&](auto r) { out = std::move(r); });
        engine.cancel_renders(pdf::RenderChannel::view);
        engine.wait_idle();
        engine.drain();
        REQUIRE(out.has_value());
        // Puede haberse renderizado antes de la cancelación; si no, debe decir "cancelado".
        if (!out->has_value()) {
            CHECK(out->error() == "cancelado");
        }
    }
}

TEST_CASE("Engine: búsqueda, selección de texto y objetos") {
    pdf::Engine engine{font_path(), nullptr};
    make_sample(engine, "texto.pdf");

    auto hits = sync<std::vector<pdf::SearchHit>>(
        engine, [&](auto cb) { engine.search("mundo", false, false, std::move(cb)); });
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].page == 0);
    CHECK(hits[1].page == 1);
    REQUIRE_FALSE(hits[0].rects.empty());
    // "Hola mundo" se insertó con la esquina superior izquierda en (72, 72).
    CHECK(hits[0].rects[0].y0 >= 70.0f);
    CHECK(hits[0].rects[0].y1 <= 100.0f);

    auto none = sync<std::vector<pdf::SearchHit>>(
        engine, [&](auto cb) { engine.search("MUNDO", true, false, std::move(cb)); });
    CHECK(none.empty());

    const auto r = hits[0].rects[0];
    const int idx = sync<int>(engine, [&](auto cb) {
        engine.char_index_at(0, {(r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2}, std::move(cb));
    });
    CHECK(idx >= 5);

    auto sel = sync<pdf::Result<pdf::TextSelection>>(
        engine, [&](auto cb) { engine.text_range(0, 0, 9, std::move(cb)); });
    REQUIRE(sel.has_value());
    CHECK(sel->text == "Hola mundo");
    CHECK_FALSE(sel->rects.empty());

    auto objs = sync<pdf::Result<std::vector<pdf::ObjectInfo>>>(
        engine, [&](auto cb) { engine.objects(0, std::move(cb)); });
    REQUIRE(objs.has_value());
    REQUIRE(objs->size() == 1);
    CHECK(objs->at(0).kind == pdf::ObjectKind::text);
    CHECK(objs->at(0).text == "Hola mundo");
    CHECK(objs->at(0).font_size == doctest::Approx(24.0f).epsilon(0.01));
    CHECK(objs->at(0).bounds.x0 == doctest::Approx(72.0f).epsilon(0.02));
    CHECK(objs->at(0).bounds.y0 == doctest::Approx(72.0f).epsilon(0.02));
}

TEST_CASE("Engine: reemplazo de texto (nivel 2) con deshacer y guardado completo") {
    pdf::Engine engine{font_path(), nullptr};
    const auto path = make_sample(engine, "reemplazo.pdf");

    auto objs = sync<pdf::Result<std::vector<pdf::ObjectInfo>>>(
        engine, [&](auto cb) { engine.objects(0, std::move(cb)); });
    REQUIRE(objs.has_value());
    const auto original = objs->at(0);

    pdf::TextSpec spec;
    spec.text = "Adiós, señor";
    spec.size = original.font_size;
    spec.font = pdf::StandardFont::embedded_inter;
    auto replaced = sync<pdf::Result<pdf::ReplaceOutcome>>(
        engine, [&](auto cb) { engine.replace_text(original.id, spec, std::move(cb)); });
    REQUIRE(replaced.has_value());
    CHECK(replaced->index == 0);
    CHECK(replaced->replacement.text == "Adiós, señor");
    // Misma línea base: la caja nueva empieza donde la original.
    CHECK(replaced->replacement.bounds.x0 == doctest::Approx(original.bounds.x0).epsilon(0.05));

    SUBCASE("deshacer: quitar el nuevo y reinsertar el original") {
        auto d = sync<pdf::Result<int>>(
            engine, [&](auto cb) { engine.detach(replaced->replacement.id, std::move(cb)); });
        REQUIRE(d.has_value());
        auto a = sync<pdf::Result<pdf::ObjectInfo>>(
            engine, [&](auto cb) { engine.attach(original.id, replaced->index, std::move(cb)); });
        REQUIRE(a.has_value());
        CHECK(a->text == "Hola mundo");
        CHECK(a->id == original.id); // mismo ObjectId tras deshacer
    }

    SUBCASE("al guardar, el texto original desaparece del archivo") {
        const auto copy = out_dir() / "reemplazo-guardado.pdf";
        auto saved = sync<pdf::Result<pdf::SaveOutcome>>(
            engine, [&](auto cb) { engine.save(copy, std::move(cb)); });
        REQUIRE(saved.has_value());
        CHECK_FALSE(saved->reopened);
        auto reopened = sync<pdf::Result<pdf::DocInfo>>(
            engine, [&](auto cb) { engine.open(copy, std::move(cb)); });
        REQUIRE(reopened.has_value());
        auto old_hits = sync<std::vector<pdf::SearchHit>>(
            engine, [&](auto cb) { engine.search("Hola", false, false, std::move(cb)); });
        CHECK(old_hits.empty());
        auto new_hits = sync<std::vector<pdf::SearchHit>>(
            engine, [&](auto cb) { engine.search("señor", false, false, std::move(cb)); });
        CHECK(new_hits.size() == 1);
    }
}

TEST_CASE("Engine: mover/redimensionar e insertar imagen") {
    pdf::Engine engine{font_path(), nullptr};
    make_sample(engine, "objetos.pdf");

    pdf::ImageSpec image;
    image.bitmap.width = 4;
    image.bitmap.height = 4;
    image.bitmap.pixels.assign(4 * 4 * 4, 0);
    for (std::size_t i = 0; i < image.bitmap.pixels.size(); i += 4) {
        image.bitmap.pixels[i + 2] = 255; // rojo en BGRA
        image.bitmap.pixels[i + 3] = 255;
    }
    image.rect = {100.0f, 300.0f, 300.0f, 400.0f};
    auto img = sync<pdf::Result<pdf::ObjectInfo>>(
        engine, [&](auto cb) { engine.insert_image(2, image, std::move(cb)); });
    REQUIRE(img.has_value());
    CHECK(img->kind == pdf::ObjectKind::image);
    CHECK(img->bounds.x0 == doctest::Approx(100.0f).epsilon(0.01));
    CHECK(img->bounds.y0 == doctest::Approx(300.0f).epsilon(0.01));
    CHECK(img->bounds.x1 == doctest::Approx(300.0f).epsilon(0.01));
    CHECK(img->bounds.y1 == doctest::Approx(400.0f).epsilon(0.01));

    auto rendered = sync<pdf::Result<pdf::RenderedPage>>(
        engine, [&](auto cb) { engine.render(2, 1.0f, pdf::RenderChannel::view, std::move(cb)); });
    REQUIRE(rendered.has_value());
    // Píxel en el centro de la imagen: rojo (B=0, G=0, R=255).
    const auto& px = rendered->bitmap.pixels;
    const std::size_t at = (350u * static_cast<std::size_t>(rendered->bitmap.width) + 200u) * 4u;
    CHECK(px[at + 0] < 30);
    CHECK(px[at + 1] < 30);
    CHECK(px[at + 2] > 220);

    const pdf::Rect target{50.0f, 500.0f, 150.0f, 550.0f};
    auto moved = sync<pdf::Result<pdf::ObjectInfo>>(
        engine, [&](auto cb) { engine.set_bounds(img->id, target, std::move(cb)); });
    REQUIRE(moved.has_value());
    CHECK(moved->bounds.x0 == doctest::Approx(50.0f).epsilon(0.01));
    CHECK(moved->bounds.y0 == doctest::Approx(500.0f).epsilon(0.01));
    CHECK(moved->bounds.x1 == doctest::Approx(150.0f).epsilon(0.01));
    CHECK(moved->bounds.y1 == doctest::Approx(550.0f).epsilon(0.01));
}

TEST_CASE("Engine: anotaciones visibles en el render") {
    pdf::Engine engine{font_path(), nullptr};
    make_sample(engine, "anotaciones.pdf");

    pdf::AnnotSpec hl;
    hl.kind = pdf::AnnotKind::highlight;
    hl.color = {255, 255, 0, 255};
    hl.rects = {{300.0f, 600.0f, 500.0f, 640.0f}};
    auto added = sync<pdf::Result<int>>(
        engine, [&](auto cb) { engine.add_annotation(2, hl, std::move(cb)); });
    REQUIRE(added.has_value());
    CHECK(*added == 0);

    auto rendered = sync<pdf::Result<pdf::RenderedPage>>(
        engine, [&](auto cb) { engine.render(2, 1.0f, pdf::RenderChannel::view, std::move(cb)); });
    REQUIRE(rendered.has_value());
    const std::size_t at = (620u * static_cast<std::size_t>(rendered->bitmap.width) + 400u) * 4u;
    const auto& px = rendered->bitmap.pixels;
    // Amarillo sobre blanco: azul bajo, rojo y verde altos.
    CHECK(px[at + 0] < 128);
    CHECK(px[at + 1] > 200);
    CHECK(px[at + 2] > 200);

    auto removed = sync<pdf::Result<void>>(
        engine, [&](auto cb) { engine.remove_annotation(2, *added, std::move(cb)); });
    CHECK(removed.has_value());
}

TEST_CASE("Engine: operaciones de páginas") {
    pdf::Engine engine{font_path(), nullptr};
    const auto path = make_sample(engine, "paginas.pdf");

    auto rotated = sync<pdf::Result<pdf::DocInfo>>(
        engine, [&](auto cb) { engine.rotate_page(0, 1, std::move(cb)); });
    REQUIRE(rotated.has_value());
    CHECK(rotated->pages[0].width == doctest::Approx(842.0f));
    CHECK(rotated->pages[0].height == doctest::Approx(595.0f));

    auto moved = sync<pdf::Result<pdf::DocInfo>>(
        engine, [&](auto cb) { engine.move_page(0, 2, std::move(cb)); });
    REQUIRE(moved.has_value());
    CHECK(moved->pages[2].width == doctest::Approx(842.0f));
    auto hits = sync<std::vector<pdf::SearchHit>>(
        engine, [&](auto cb) { engine.search("Hola", false, false, std::move(cb)); });
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].page == 2);

    auto merged = sync<pdf::Result<pdf::DocInfo>>(
        engine, [&](auto cb) { engine.import_pages(path, 3, std::move(cb)); });
    REQUIRE(merged.has_value());
    CHECK(merged->pages.size() == 6);

    auto deleted = sync<pdf::Result<pdf::DocInfo>>(
        engine, [&](auto cb) { engine.delete_pages({0, 5}, std::move(cb)); });
    REQUIRE(deleted.has_value());
    CHECK(deleted->pages.size() == 4);

    auto all = sync<pdf::Result<pdf::DocInfo>>(
        engine, [&](auto cb) { engine.delete_pages({0, 1, 2, 3}, std::move(cb)); });
    CHECK_FALSE(all.has_value()); // no se puede dejar un PDF sin páginas

    const auto extracted = out_dir() / "extraido.pdf";
    auto ex = sync<pdf::Result<void>>(
        engine, [&](auto cb) { engine.extract_pages({0, 1}, extracted, std::move(cb)); });
    REQUIRE(ex.has_value());
    auto info = sync<pdf::Result<pdf::DocInfo>>(
        engine, [&](auto cb) { engine.open(extracted, std::move(cb)); });
    REQUIRE(info.has_value());
    CHECK(info->pages.size() == 2);
}

TEST_CASE("Engine: rutas con acentos y ñ") {
    pdf::Engine engine{font_path(), nullptr};
    const auto dir = out_dir() / std::filesystem::path{u8"carpeta año ñ"};
    std::filesystem::create_directories(dir);
    const auto path = dir / std::filesystem::path{u8"canción.pdf"};
    auto created = sync<pdf::Result<void>>(
        engine, [&](auto cb) { engine.create_blank(path, 1, 200.0f, 200.0f, std::move(cb)); });
    REQUIRE(created.has_value());
    auto info =
        sync<pdf::Result<pdf::DocInfo>>(engine, [&](auto cb) { engine.open(path, std::move(cb)); });
    REQUIRE(info.has_value());
    CHECK(info->pages.size() == 1);
}
