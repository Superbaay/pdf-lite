#pragma once

// Tipos públicos de la capa PDF. No incluyen nada de PDFium: es lo único que
// ven el resto de módulos.
//
// Sistema de coordenadas "de vista": puntos PDF (1/72") con origen en la
// esquina SUPERIOR izquierda de la página tal como se muestra (ya rotada).
// La conversión a coordenadas internas del PDF (origen abajo a la izquierda,
// sin rotar) la hace la Session con FPDF_PageToDevice/FPDF_DeviceToPage.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pdf {

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    float x0 = 0.0f; // izquierda
    float y0 = 0.0f; // arriba
    float x1 = 0.0f; // derecha
    float y1 = 0.0f; // abajo

    [[nodiscard]] float width() const { return x1 - x0; }
    [[nodiscard]] float height() const { return y1 - y0; }
    [[nodiscard]] bool contains(Point p) const {
        return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1;
    }
};

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
    bool operator==(const Color&) const = default;
};

struct PageSize {
    float width = 0.0f; // puntos, ya con la rotación de la página aplicada
    float height = 0.0f;
};

struct DocInfo {
    std::filesystem::path path;
    std::vector<PageSize> pages;
    std::string title;
};

// Imagen BGRA de 32 bits sin padding (stride == width * 4).
struct Bitmap {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

struct RenderedPage {
    int page = 0;
    float scale = 1.0f;         // píxeles por punto
    std::uint64_t revision = 0; // revisión de la página cuando se renderizó
    Bitmap bitmap;
};

struct OutlineItem {
    std::string title;
    int page = -1; // -1 si el marcador no apunta a una página
    std::vector<OutlineItem> children;
};

struct SearchHit {
    int page = 0;
    std::vector<Rect> rects; // una coincidencia puede ocupar varias líneas
};

struct TextSelection {
    int page = 0;
    std::vector<Rect> rects;
    std::string text; // UTF-8
};

enum class StandardFont : std::uint8_t {
    helvetica,
    helvetica_bold,
    times,
    times_bold,
    courier,
    embedded_inter, // TTF incrustada: cubre cualquier carácter Unicode de Inter
};

enum class ObjectKind : std::uint8_t { text, image, other };

// Identificador opaco de un objeto de página. Estable mientras el documento
// siga abierto (incluso si el objeto se quita y se vuelve a insertar con undo).
struct ObjectId {
    int page = -1;
    std::uint64_t handle = 0;
    bool operator==(const ObjectId&) const = default;
    [[nodiscard]] bool valid() const { return page >= 0 && handle != 0; }
};

struct ObjectInfo {
    ObjectId id;
    ObjectKind kind = ObjectKind::other;
    Rect bounds;
    // Solo texto:
    std::string text;
    float font_size = 0.0f; // tamaño efectivo (tamaño × escala de la matriz)
    Color color;
    std::string font_name;
};

struct TextSpec {
    std::string text; // UTF-8
    StandardFont font = StandardFont::helvetica;
    float size = 12.0f;
    Color color;
    Point origin; // esquina superior izquierda del texto, coordenadas de vista
};

struct ImageSpec {
    Bitmap bitmap;
    Rect rect; // coordenadas de vista
};

enum class AnnotKind : std::uint8_t { highlight, underline, strikeout, note, ink };

struct AnnotSpec {
    AnnotKind kind = AnnotKind::highlight;
    Color color{255, 220, 0, 255};
    std::vector<Rect> rects;                 // resaltado/subrayado/tachado
    Point position;                          // nota
    std::string contents;                    // nota
    std::vector<std::vector<Point>> strokes; // dibujo libre
};

} // namespace pdf
