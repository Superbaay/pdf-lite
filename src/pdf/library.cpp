#include "pdf/library.hpp"

#include "pdf/internal/handles.hpp"

#include <fpdf_save.h>

namespace pdf {

using internal::DocumentPtr;
using internal::PagePtr;

Library::Library() {
    FPDF_LIBRARY_CONFIG config{};
    config.version = 2;
    config.m_pUserFontPaths = nullptr;
    config.m_pIsolate = nullptr;
    config.m_v8EmbedderSlot = 0;
    FPDF_InitLibraryWithConfig(&config);
}

Library::~Library() {
    FPDF_DestroyLibrary();
}

std::string last_error_message() {
    switch (FPDF_GetLastError()) {
    case FPDF_ERR_SUCCESS:
        return "sin error";
    case FPDF_ERR_FILE:
        return "no se pudo abrir el archivo";
    case FPDF_ERR_FORMAT:
        return "el archivo no es un PDF válido o está dañado";
    case FPDF_ERR_PASSWORD:
        return "el PDF requiere contraseña";
    case FPDF_ERR_SECURITY:
        return "esquema de seguridad no soportado";
    case FPDF_ERR_PAGE:
        return "página no encontrada o con contenido dañado";
    default:
        return "error desconocido de PDFium";
    }
}

namespace {

// PDFium escribe el archivo llamando a un callback de C. Ponemos la estructura
// FPDF_FILEWRITE como PRIMER miembro para poder convertir el puntero `self`
// de vuelta a nuestro tipo (patrón habitual en APIs de C con callbacks).
struct VectorWriter {
    FPDF_FILEWRITE base{};
    std::vector<std::uint8_t>* out = nullptr;

    static int write_block(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
        auto* writer = reinterpret_cast<VectorWriter*>(self);
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        writer->out->insert(writer->out->end(), bytes, bytes + size);
        return 1;
    }
};

} // namespace

std::expected<std::vector<std::uint8_t>, std::string> make_blank_pdf(int pages, double width_pt,
                                                                     double height_pt) {
    DocumentPtr doc{FPDF_CreateNewDocument()};
    if (!doc) {
        return std::unexpected{"no se pudo crear el documento"};
    }
    for (int i = 0; i < pages; ++i) {
        PagePtr page{FPDFPage_New(doc.get(), i, width_pt, height_pt)};
        if (!page) {
            return std::unexpected{"no se pudo crear la página"};
        }
    }

    std::vector<std::uint8_t> bytes;
    VectorWriter writer;
    writer.base.version = 1;
    writer.base.WriteBlock = &VectorWriter::write_block;
    writer.out = &bytes;
    if (!FPDF_SaveAsCopy(doc.get(), &writer.base, FPDF_NO_INCREMENTAL)) {
        return std::unexpected{"no se pudo serializar el documento"};
    }
    return bytes;
}

std::expected<int, std::string> count_pages(std::span<const std::uint8_t> bytes) {
    DocumentPtr doc{FPDF_LoadMemDocument64(bytes.data(), bytes.size(), nullptr)};
    if (!doc) {
        return std::unexpected{last_error_message()};
    }
    return FPDF_GetPageCount(doc.get());
}

} // namespace pdf
