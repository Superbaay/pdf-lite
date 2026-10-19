#include "pdf/library.hpp"

#include <doctest/doctest.h>

#include <array>

TEST_CASE("PDFium crea y vuelve a leer un PDF en memoria") {
    const pdf::Library library;

    auto bytes = pdf::make_blank_pdf(3, 612.0, 792.0); // carta en puntos
    REQUIRE(bytes.has_value());
    CHECK(bytes->size() > 0);

    auto pages = pdf::count_pages(*bytes);
    REQUIRE(pages.has_value());
    CHECK(*pages == 3);
}

TEST_CASE("Un archivo que no es PDF devuelve un error legible") {
    const pdf::Library library;

    constexpr std::array<std::uint8_t, 5> garbage{'h', 'o', 'l', 'a', '!'};
    auto pages = pdf::count_pages(garbage);
    REQUIRE_FALSE(pages.has_value());
    CHECK(pages.error() == "el archivo no es un PDF válido o está dañado");
}
