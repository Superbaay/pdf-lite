#pragma once

#include "core/concurrent_queue.hpp"
#include "pdf/types.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace pdf {

namespace internal {
class Session;
}

template <typename T>
using Result = std::expected<T, std::string>;

template <typename T>
using Callback = std::move_only_function<void(T)>;

struct ReplaceOutcome {
    int index = 0;
    ObjectInfo replacement;
};

struct SaveOutcome {
    DocInfo info;
    bool reopened = false;
};

// Canales de renderizado: cancelar los de la vista principal (p. ej. al hacer
// zoom) no debe cancelar las miniaturas, y viceversa.
enum class RenderChannel : std::uint8_t { view = 0, thumbnails = 1 };

// Fachada asíncrona sobre PDFium.
//
// PDFium no es thread-safe, así que TODAS sus llamadas ocurren en un único
// hilo propio (el "PDF worker"). Cada método encola un trabajo y vuelve de
// inmediato; el resultado llega como callback que se ejecuta en el hilo de UI
// cuando éste llama a drain(). Así la interfaz nunca se bloquea.
//
//   UI ──job──▶ [cola de trabajos] ──▶ worker (PDFium)
//   UI ◀─drain─ [cola de resultados] ◀── worker
class Engine {
public:
    // `wake_ui` se llama DESDE EL WORKER cuando hay resultados: debe ser
    // thread-safe (usamos SDL_PushEvent, que lo es).
    Engine(std::filesystem::path embedded_font, std::function<void()> wake_ui);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    // Ejecuta en el hilo actual (UI) los callbacks pendientes. Devuelve cuántos.
    int drain();

    // Espera a que el worker termine todo lo encolado (solo para pruebas).
    void wait_idle();

    // --- Documento
    void open(std::filesystem::path path, Callback<Result<DocInfo>> done);
    void close();

    // --- Lectura
    void render(int page, float scale, RenderChannel channel, Callback<Result<RenderedPage>> done);
    // Invalida los renders pendientes de un canal (se responden con error "cancelado").
    void cancel_renders(RenderChannel channel);
    void outline(Callback<std::vector<OutlineItem>> done);
    void search(std::string query, bool match_case, bool whole_word,
                Callback<std::vector<SearchHit>> done);
    void char_index_at(int page, Point p, Callback<int> done);
    void text_range(int page, int from, int to, Callback<Result<TextSelection>> done);
    void objects(int page, Callback<Result<std::vector<ObjectInfo>>> done);

    // --- Edición
    void insert_text(int page, TextSpec spec, Callback<Result<ObjectInfo>> done);
    void insert_image(int page, ImageSpec spec, Callback<Result<ObjectInfo>> done);
    void detach(ObjectId id, Callback<Result<int>> done);
    void attach(ObjectId id, int index, Callback<Result<ObjectInfo>> done);
    void set_bounds(ObjectId id, Rect bounds, Callback<Result<ObjectInfo>> done);
    void replace_text(ObjectId id, TextSpec spec, Callback<Result<ReplaceOutcome>> done);
    void add_annotation(int page, AnnotSpec spec, Callback<Result<int>> done);
    void remove_annotation(int page, int index, Callback<Result<void>> done);

    // --- Páginas
    void rotate_page(int page, int quarter_turns, Callback<Result<DocInfo>> done);
    void delete_pages(std::vector<int> pages, Callback<Result<DocInfo>> done);
    void move_page(int from, int to, Callback<Result<DocInfo>> done);
    void import_pages(std::filesystem::path source, int at_index, Callback<Result<DocInfo>> done);
    void extract_pages(std::vector<int> pages, std::filesystem::path dest,
                       Callback<Result<void>> done);

    void save(std::filesystem::path dest, Callback<Result<SaveOutcome>> done);

    // Crea un PDF nuevo en blanco en `dest` (útil para pruebas y "nuevo documento").
    void create_blank(std::filesystem::path dest, int pages, float width, float height,
                      Callback<Result<void>> done);

private:
    using Job = std::move_only_function<void(internal::Session&)>;
    using Continuation = std::move_only_function<void()>;

    // Encola `work` (se ejecuta en el worker) y entrega su resultado a `done` en la UI.
    template <typename R, typename Work>
    void submit(Work work, Callback<R> done);

    void post(Job job);
    void thread_main(std::filesystem::path font);

    core::ConcurrentQueue<Job> m_jobs;
    core::ConcurrentQueue<Continuation> m_results;
    std::function<void()> m_wake;
    std::array<std::atomic<std::uint64_t>, 2> m_render_generation{};
    std::thread m_thread; // último miembro: arranca cuando todo lo demás ya existe
};

} // namespace pdf
