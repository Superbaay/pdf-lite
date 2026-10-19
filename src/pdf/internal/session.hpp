#pragma once

// Session: documento abierto + toda la lógica que llama a PDFium.
// INTERNO de src/pdf/. Solo se usa desde el hilo PDF worker.

#include "pdf/internal/handles.hpp"
#include "pdf/types.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pdf::internal {

template <typename T>
using Result = std::expected<T, std::string>;

// Lector de archivo para FPDF_LoadCustomDocument: PDFium pide bloques bajo
// demanda, así un PDF de 200 MB no se carga entero en RAM.
// Usamos std::ifstream con std::filesystem::path para soportar rutas Unicode
// (FPDF_LoadDocument recibe char* y en Windows no maneja bien acentos).
class FileReader {
public:
    static Result<std::unique_ptr<FileReader>> open(const std::filesystem::path& path);
    FPDF_FILEACCESS* access() { return &m_access; }

    explicit FileReader(std::ifstream stream, unsigned long size);

private:
    static int get_block(void* param, unsigned long position, unsigned char* buffer,
                         unsigned long size);

    std::ifstream m_stream;
    FPDF_FILEACCESS m_access{};
};

struct ReplaceResult {
    int index = 0; // posición (orden de dibujo) del objeto original
    ObjectInfo replacement;
};

struct SaveResult {
    DocInfo info;
    bool reopened = false; // true: los ObjectId anteriores dejan de ser válidos
};

class Session {
public:
    explicit Session(std::filesystem::path embedded_font_path);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    Result<DocInfo> open(const std::filesystem::path& path);
    void close();
    [[nodiscard]] bool is_open() const { return m_doc != nullptr; }

    Result<RenderedPage> render(int page, float scale);
    std::vector<OutlineItem> outline();
    std::vector<SearchHit> search(const std::string& query, bool match_case, bool whole_word);
    int char_index_at(int page, Point p, float x_tolerance, float y_tolerance);
    Result<TextSelection> text_range(int page, int from, int to);
    Result<std::vector<ObjectInfo>> objects(int page);
    Result<ObjectInfo> object_info(ObjectId id);

    // Edición (nivel 1 y 2)
    Result<ObjectInfo> insert_text(int page, const TextSpec& spec);
    Result<ObjectInfo> insert_image(int page, const ImageSpec& spec);
    Result<int> detach(ObjectId id);
    Result<ObjectInfo> attach(ObjectId id, int index);
    Result<ObjectInfo> set_bounds(ObjectId id, Rect bounds);
    Result<ReplaceResult> replace_text(ObjectId id, const TextSpec& spec);

    // Anotaciones
    Result<int> add_annotation(int page, const AnnotSpec& spec);
    Result<void> remove_annotation(int page, int index);

    // Páginas
    Result<DocInfo> rotate_page(int page, int quarter_turns);
    Result<DocInfo> delete_pages(std::vector<int> pages);
    Result<DocInfo> move_page(int from, int to);
    Result<DocInfo> import_pages(const std::filesystem::path& source, int at_index);
    Result<void> extract_pages(const std::vector<int>& pages, const std::filesystem::path& dest);

    Result<SaveResult> save(const std::filesystem::path& dest);

    // Escribe un PDF nuevo con páginas en blanco (no toca el documento abierto).
    static Result<void> create_blank(const std::filesystem::path& dest, int pages, float width,
                                     float height);

    [[nodiscard]] std::uint64_t revision(int page) const;

private:
    struct LoadedPage {
        PagePtr page;
        TextPagePtr text;
        bool pinned = false; // editada: no se descarga (sus ObjectId deben seguir vivos)
        std::uint64_t last_use = 0;
    };

    Result<FPDF_PAGE> page(int index);
    Result<FPDF_TEXTPAGE> text_page(int index);
    void mark_edited(int index); // genera contenido, fija la página e invalida texto
    void unload_pages();
    void destroy_detached();
    DocInfo make_info() const;
    Result<FPDF_PAGEOBJECT> find_object(ObjectId id, int* index_out = nullptr);
    ObjectInfo describe(int page_index, FPDF_PAGEOBJECT obj);
    Result<FPDF_PAGEOBJECT> create_text_object(const TextSpec& spec);
    Result<FPDF_FONT> embedded_font();
    bool in_range(int page) const;

    // Conversión entre coordenadas de vista y de PDF (ver types.hpp).
    Point to_pdf(FPDF_PAGE page, int index, Point view) const;
    Point to_view(FPDF_PAGE page, int index, Point pdf) const;
    Rect rect_to_view(FPDF_PAGE page, int index, float left, float bottom, float right,
                      float top) const;

    std::filesystem::path m_font_path;
    std::filesystem::path m_source; // archivo del que lee PDFium
    std::filesystem::path m_path;   // ruta "actual" (cambia con Guardar como)
    std::unique_ptr<FileReader> m_reader;
    DocumentPtr m_doc;
    std::map<int, LoadedPage> m_pages;
    std::vector<PageObjectPtr> m_detached; // objetos quitados de su página (undo)
    std::vector<std::uint8_t> m_font_bytes;
    FontPtr m_font;
    std::vector<PageSize> m_sizes;
    std::vector<std::uint64_t> m_revisions;
    std::uint64_t m_use_counter = 0;
};

} // namespace pdf::internal
