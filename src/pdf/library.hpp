#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace pdf {

// Inicializa PDFium al construirse y lo libera al destruirse (RAII).
// Debe existir exactamente una instancia, creada y destruida en el hilo que
// hará todas las llamadas a PDFium (PDFium no es thread-safe).
class Library {
public:
    Library();
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    Library(Library&&) = delete;
    Library& operator=(Library&&) = delete;
};

// Traduce FPDF_GetLastError() a un mensaje en español.
std::string last_error_message();

// Crea en memoria un PDF con `pages` páginas en blanco (tamaño en puntos).
// Pensado para pruebas: así el repo no necesita PDFs de ejemplo.
std::expected<std::vector<std::uint8_t>, std::string> make_blank_pdf(int pages, double width_pt,
                                                                     double height_pt);

// Abre un PDF desde memoria y devuelve su número de páginas.
std::expected<int, std::string> count_pages(std::span<const std::uint8_t> bytes);

} // namespace pdf
