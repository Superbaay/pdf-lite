#include "pdf/engine.hpp"

#include "pdf/internal/session.hpp"
#include "pdf/library.hpp"

#include <future>

namespace pdf {

using internal::Session;

Engine::Engine(std::filesystem::path embedded_font, std::function<void()> wake_ui)
    : m_wake{std::move(wake_ui)}, m_thread{[this, font = std::move(embedded_font)]() mutable {
          thread_main(std::move(font));
      }} {}

Engine::~Engine() {
    // Cerrar la cola despierta al worker; procesa lo que queda y sale. Los
    // callbacks pendientes se descartan (la UI ya no existe para recibirlos).
    m_jobs.close();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_results.close();
}

void Engine::thread_main(std::filesystem::path font) {
    // Library y Session viven en la pila de ESTE hilo: se crean y destruyen
    // aquí, de modo que ni siquiera la inicialización de PDFium cruza de hilo.
    const Library library;
    Session session{std::move(font)};
    while (auto job = m_jobs.wait_pop()) {
        (*job)(session);
    }
}

void Engine::post(Job job) {
    m_jobs.push(std::move(job));
}

template <typename R, typename Work>
void Engine::submit(Work work, Callback<R> done) {
    post([this, work = std::move(work), done = std::move(done)](Session& s) mutable {
        R result = work(s);
        m_results.push([done = std::move(done), result = std::move(result)]() mutable {
            done(std::move(result));
        });
        if (m_wake) {
            m_wake();
        }
    });
}

int Engine::drain() {
    int count = 0;
    while (auto continuation = m_results.try_pop()) {
        (*continuation)();
        ++count;
    }
    return count;
}

void Engine::wait_idle() {
    std::promise<void> done;
    auto future = done.get_future();
    post([&done](Session&) { done.set_value(); });
    future.wait();
}

// --- Documento ---------------------------------------------------------------

void Engine::open(std::filesystem::path path, Callback<Result<DocInfo>> done) {
    // Abrir otro documento invalida todo render pendiente del anterior.
    cancel_renders(RenderChannel::view);
    cancel_renders(RenderChannel::thumbnails);
    submit<Result<DocInfo>>([path = std::move(path)](Session& s) { return s.open(path); },
                            std::move(done));
}

void Engine::close() {
    cancel_renders(RenderChannel::view);
    cancel_renders(RenderChannel::thumbnails);
    post([](Session& s) { s.close(); });
}

// --- Lectura ------------------------------------------------------------------

void Engine::render(int page, float scale, RenderChannel channel,
                    Callback<Result<RenderedPage>> done) {
    auto& generation = m_render_generation[static_cast<std::size_t>(channel)];
    const std::uint64_t ticket = generation.load();
    submit<Result<RenderedPage>>(
        [page, scale, ticket, &generation](Session& s) -> Result<RenderedPage> {
            // Si la UI pidió cancelar después de encolar esto, no gastamos CPU.
            if (generation.load() != ticket) {
                return std::unexpected{"cancelado"};
            }
            return s.render(page, scale);
        },
        std::move(done));
}

void Engine::cancel_renders(RenderChannel channel) {
    ++m_render_generation[static_cast<std::size_t>(channel)];
}

void Engine::outline(Callback<std::vector<OutlineItem>> done) {
    submit<std::vector<OutlineItem>>([](Session& s) { return s.outline(); }, std::move(done));
}

void Engine::search(std::string query, bool match_case, bool whole_word,
                    Callback<std::vector<SearchHit>> done) {
    submit<std::vector<SearchHit>>(
        [query = std::move(query), match_case, whole_word](Session& s) {
            return s.search(query, match_case, whole_word);
        },
        std::move(done));
}

void Engine::char_index_at(int page, Point p, Callback<int> done) {
    submit<int>(
        [page,
         p](Session& s) { // Tolerancia mayor en horizontal: empezar a seleccionar un poco a la
            // izquierda del texto debe tomar el primer carácter de la línea.
            return s.char_index_at(page, p, 12.0f, 5.0f);
        },
        std::move(done));
}

void Engine::text_range(int page, int from, int to, Callback<Result<TextSelection>> done) {
    submit<Result<TextSelection>>(
        [page, from, to](Session& s) { return s.text_range(page, from, to); }, std::move(done));
}

void Engine::objects(int page, Callback<Result<std::vector<ObjectInfo>>> done) {
    submit<Result<std::vector<ObjectInfo>>>([page](Session& s) { return s.objects(page); },
                                            std::move(done));
}

// --- Edición ------------------------------------------------------------------

void Engine::insert_text(int page, TextSpec spec, Callback<Result<ObjectInfo>> done) {
    submit<Result<ObjectInfo>>(
        [page, spec = std::move(spec)](Session& s) { return s.insert_text(page, spec); },
        std::move(done));
}

void Engine::insert_image(int page, ImageSpec spec, Callback<Result<ObjectInfo>> done) {
    submit<Result<ObjectInfo>>(
        [page, spec = std::move(spec)](Session& s) { return s.insert_image(page, spec); },
        std::move(done));
}

void Engine::detach(ObjectId id, Callback<Result<int>> done) {
    submit<Result<int>>([id](Session& s) { return s.detach(id); }, std::move(done));
}

void Engine::attach(ObjectId id, int index, Callback<Result<ObjectInfo>> done) {
    submit<Result<ObjectInfo>>([id, index](Session& s) { return s.attach(id, index); },
                               std::move(done));
}

void Engine::set_bounds(ObjectId id, Rect bounds, Callback<Result<ObjectInfo>> done) {
    submit<Result<ObjectInfo>>([id, bounds](Session& s) { return s.set_bounds(id, bounds); },
                               std::move(done));
}

void Engine::replace_text(ObjectId id, TextSpec spec, Callback<Result<ReplaceOutcome>> done) {
    submit<Result<ReplaceOutcome>>(
        [id, spec = std::move(spec)](Session& s) -> Result<ReplaceOutcome> {
            auto r = s.replace_text(id, spec);
            if (!r) {
                return std::unexpected{r.error()};
            }
            return ReplaceOutcome{r->index, std::move(r->replacement)};
        },
        std::move(done));
}

void Engine::add_annotation(int page, AnnotSpec spec, Callback<Result<int>> done) {
    submit<Result<int>>(
        [page, spec = std::move(spec)](Session& s) { return s.add_annotation(page, spec); },
        std::move(done));
}

void Engine::remove_annotation(int page, int index, Callback<Result<void>> done) {
    submit<Result<void>>([page, index](Session& s) { return s.remove_annotation(page, index); },
                         std::move(done));
}

// --- Páginas ------------------------------------------------------------------

void Engine::rotate_page(int page, int quarter_turns, Callback<Result<DocInfo>> done) {
    submit<Result<DocInfo>>(
        [page, quarter_turns](Session& s) { return s.rotate_page(page, quarter_turns); },
        std::move(done));
}

void Engine::delete_pages(std::vector<int> pages, Callback<Result<DocInfo>> done) {
    cancel_renders(RenderChannel::view);
    cancel_renders(RenderChannel::thumbnails);
    submit<Result<DocInfo>>(
        [pages = std::move(pages)](Session& s) mutable { return s.delete_pages(std::move(pages)); },
        std::move(done));
}

void Engine::move_page(int from, int to, Callback<Result<DocInfo>> done) {
    cancel_renders(RenderChannel::view);
    cancel_renders(RenderChannel::thumbnails);
    submit<Result<DocInfo>>([from, to](Session& s) { return s.move_page(from, to); },
                            std::move(done));
}

void Engine::import_pages(std::filesystem::path source, int at_index,
                          Callback<Result<DocInfo>> done) {
    cancel_renders(RenderChannel::view);
    cancel_renders(RenderChannel::thumbnails);
    submit<Result<DocInfo>>([source = std::move(source),
                             at_index](Session& s) { return s.import_pages(source, at_index); },
                            std::move(done));
}

void Engine::extract_pages(std::vector<int> pages, std::filesystem::path dest,
                           Callback<Result<void>> done) {
    submit<Result<void>>([pages = std::move(pages), dest = std::move(dest)](
                             Session& s) { return s.extract_pages(pages, dest); },
                         std::move(done));
}

void Engine::save(std::filesystem::path dest, Callback<Result<SaveOutcome>> done) {
    submit<Result<SaveOutcome>>(
        [dest = std::move(dest)](Session& s) -> Result<SaveOutcome> {
            auto r = s.save(dest);
            if (!r) {
                return std::unexpected{r.error()};
            }
            return SaveOutcome{std::move(r->info), r->reopened};
        },
        std::move(done));
}

void Engine::create_blank(std::filesystem::path dest, int pages, float width, float height,
                          Callback<Result<void>> done) {
    submit<Result<void>>(
        [dest = std::move(dest), pages, width, height](Session&) {
            return Session::create_blank(dest, pages, width, height);
        },
        std::move(done));
}

} // namespace pdf
