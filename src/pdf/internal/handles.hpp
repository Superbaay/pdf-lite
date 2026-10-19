#pragma once

// Envoltorios RAII sobre los handles de PDFium.
//
// INTERNO de src/pdf/: ningún otro módulo debe incluir este archivo (ni puede:
// los headers de PDFium solo están en el include path de pdflite_pdf).
//
// Idea clave (RAII): cada handle de C se guarda en un std::unique_ptr cuyo
// "deleter" llama a la función de cierre de PDFium. Así el recurso se libera
// automáticamente al salir de ámbito, incluso en retornos tempranos, y nunca
// escribimos FPDF_Close*() a mano.

#include <fpdf_edit.h>
#include <fpdf_text.h>
#include <fpdfview.h>

#include <memory>
#include <type_traits>

namespace pdf::internal {

struct DocumentCloser {
    void operator()(FPDF_DOCUMENT doc) const noexcept { FPDF_CloseDocument(doc); }
};
struct PageCloser {
    void operator()(FPDF_PAGE page) const noexcept { FPDF_ClosePage(page); }
};
struct BitmapCloser {
    void operator()(FPDF_BITMAP bmp) const noexcept { FPDFBitmap_Destroy(bmp); }
};
struct TextPageCloser {
    void operator()(FPDF_TEXTPAGE text) const noexcept { FPDFText_ClosePage(text); }
};

// FPDF_DOCUMENT es "struct fpdf_document_t__*": unique_ptr necesita el tipo
// apuntado, por eso usamos remove_pointer_t.
using DocumentPtr = std::unique_ptr<std::remove_pointer_t<FPDF_DOCUMENT>, DocumentCloser>;
using PagePtr = std::unique_ptr<std::remove_pointer_t<FPDF_PAGE>, PageCloser>;
using BitmapPtr = std::unique_ptr<std::remove_pointer_t<FPDF_BITMAP>, BitmapCloser>;
using TextPagePtr = std::unique_ptr<std::remove_pointer_t<FPDF_TEXTPAGE>, TextPageCloser>;

} // namespace pdf::internal
