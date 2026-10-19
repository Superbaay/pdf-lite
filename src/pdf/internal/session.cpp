#include "pdf/internal/session.hpp"

#include "core/utf.hpp"
#include "pdf/library.hpp"

#include <fpdf_doc.h>
#include <fpdf_ppo.h>
#include <fpdf_save.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace pdf::internal {

namespace {

// Subdivisiones de punto al convertir coordenadas: FPDF_PageToDevice trabaja
// con enteros, así que usamos un "dispositivo" de 32 px por punto (1/32 pt de precisión).
constexpr double coord_precision = 32.0;
// Límite del lado de un bitmap renderizado (evita reservar cientos de MB a zoom alto).
constexpr int max_bitmap_side = 8192;
// Páginas no editadas que se mantienen cargadas a la vez.
constexpr std::size_t max_loaded_pages = 24;

FPDF_WIDESTRING as_widestring(const std::u16string& s) {
    return reinterpret_cast<FPDF_WIDESTRING>(s.c_str());
}

const char* standard_font_name(StandardFont font) {
    switch (font) {
    case StandardFont::helvetica_bold:
        return "Helvetica-Bold";
    case StandardFont::times:
        return "Times-Roman";
    case StandardFont::times_bold:
        return "Times-Bold";
    case StandardFont::courier:
        return "Courier";
    case StandardFont::helvetica:
    case StandardFont::embedded_inter:
        break;
    }
    return "Helvetica";
}

// FPDF_FILEWRITE que escribe en un std::ofstream (ver VectorWriter en library.cpp).
struct StreamWriter {
    FPDF_FILEWRITE base{};
    std::ofstream* out = nullptr;

    static int write_block(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
        auto* writer = reinterpret_cast<StreamWriter*>(self);
        writer->out->write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        return writer->out->good() ? 1 : 0;
    }
};

Result<void> save_document(FPDF_DOCUMENT doc, const std::filesystem::path& dest) {
    std::ofstream out{dest, std::ios::binary | std::ios::trunc};
    if (!out) {
        return std::unexpected{"no se pudo crear el archivo de destino"};
    }
    StreamWriter writer;
    writer.base.version = 1;
    writer.base.WriteBlock = &StreamWriter::write_block;
    writer.out = &out;
    // Copia COMPLETA (no incremental): el contenido eliminado no queda escondido
    // en el archivo, que es lo que exige el reemplazo de texto (nivel 2).
    if (!FPDF_SaveAsCopy(doc, &writer.base, FPDF_NO_INCREMENTAL)) {
        return std::unexpected{"PDFium no pudo guardar el documento"};
    }
    out.close();
    if (!out) {
        return std::unexpected{"error al escribir en disco"};
    }
    return {};
}

bool same_path(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (std::filesystem::exists(a, ec) && std::filesystem::exists(b, ec)) {
        return std::filesystem::equivalent(a, b, ec);
    }
    return a.lexically_normal() == b.lexically_normal();
}

void outline_recursive(FPDF_DOCUMENT doc, FPDF_BOOKMARK parent, std::vector<OutlineItem>& out,
                       int depth, int& budget) {
    // Límites contra PDFs malformados con marcadores cíclicos o absurdamente profundos.
    if (depth > 32) {
        return;
    }
    for (FPDF_BOOKMARK bm = FPDFBookmark_GetFirstChild(doc, parent); bm != nullptr && budget > 0;
         bm = FPDFBookmark_GetNextSibling(doc, bm)) {
        --budget;
        OutlineItem item;
        const unsigned long bytes = FPDFBookmark_GetTitle(bm, nullptr, 0);
        if (bytes > 2) {
            std::u16string title(bytes / 2, u'\0');
            FPDFBookmark_GetTitle(bm, title.data(), bytes);
            title.resize(bytes / 2 - 1); // quitar el terminador
            item.title = core::utf16_to_utf8(title);
        }
        FPDF_DEST dest = FPDFBookmark_GetDest(doc, bm);
        if (dest == nullptr) {
            if (FPDF_ACTION action = FPDFBookmark_GetAction(bm); action != nullptr) {
                dest = FPDFAction_GetDest(doc, action);
            }
        }
        if (dest != nullptr) {
            item.page = FPDFDest_GetDestPageIndex(doc, dest);
        }
        outline_recursive(doc, bm, item.children, depth + 1, budget);
        out.push_back(std::move(item));
    }
}

} // namespace

// --- FileReader -------------------------------------------------------------

FileReader::FileReader(std::ifstream stream, unsigned long size) : m_stream{std::move(stream)} {
    m_access.m_FileLen = size;
    m_access.m_GetBlock = &FileReader::get_block;
    m_access.m_Param = this;
}

Result<std::unique_ptr<FileReader>> FileReader::open(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return std::unexpected{"no se pudo abrir el archivo: " + ec.message()};
    }
    if (size > 0xFFFFFFFFull) {
        return std::unexpected{"el archivo supera los 4 GB que admite PDFium"};
    }
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return std::unexpected{"no se pudo abrir el archivo"};
    }
    return std::make_unique<FileReader>(std::move(stream), static_cast<unsigned long>(size));
}

int FileReader::get_block(void* param, unsigned long position, unsigned char* buffer,
                          unsigned long size) {
    auto* self = static_cast<FileReader*>(param);
    self->m_stream.clear();
    self->m_stream.seekg(static_cast<std::streamoff>(position));
    self->m_stream.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size));
    return self->m_stream.gcount() == static_cast<std::streamsize>(size) ? 1 : 0;
}

// --- Session: ciclo de vida --------------------------------------------------

Session::Session(std::filesystem::path embedded_font_path)
    : m_font_path{std::move(embedded_font_path)} {}

Session::~Session() {
    close();
}

Result<DocInfo> Session::open(const std::filesystem::path& path) {
    close();
    auto reader = FileReader::open(path);
    if (!reader) {
        return std::unexpected{reader.error()};
    }
    DocumentPtr doc{FPDF_LoadCustomDocument((*reader)->access(), nullptr)};
    if (!doc) {
        return std::unexpected{last_error_message()};
    }
    m_reader = std::move(*reader);
    m_doc = std::move(doc);
    m_source = path;
    m_path = path;
    const int count = FPDF_GetPageCount(m_doc.get());
    m_sizes.assign(static_cast<std::size_t>(std::max(0, count)), PageSize{});
    for (int i = 0; i < count; ++i) {
        FS_SIZEF size{};
        FPDF_GetPageSizeByIndexF(m_doc.get(), i, &size);
        m_sizes[static_cast<std::size_t>(i)] = {size.width, size.height};
    }
    m_revisions.assign(m_sizes.size(), 0);
    return make_info();
}

void Session::close() {
    // Orden importa: páginas y objetos antes que el documento; el documento
    // antes que el lector de archivo del que PDFium sigue leyendo.
    unload_pages();
    destroy_detached();
    m_font.reset();
    m_doc.reset();
    m_reader.reset();
    m_sizes.clear();
    m_revisions.clear();
}

void Session::unload_pages() {
    for (auto& [index, loaded] : m_pages) {
        loaded.text.reset();
        loaded.page.reset();
    }
    m_pages.clear();
}

void Session::destroy_detached() {
    m_detached.clear(); // PageObjectPtr llama a FPDFPageObj_Destroy
}

DocInfo Session::make_info() const {
    DocInfo info;
    info.path = m_path;
    info.pages = m_sizes;
    if (m_doc) {
        const unsigned long bytes = FPDF_GetMetaText(m_doc.get(), "Title", nullptr, 0);
        if (bytes > 2) {
            std::u16string title(bytes / 2, u'\0');
            FPDF_GetMetaText(m_doc.get(), "Title", title.data(), bytes);
            title.resize(bytes / 2 - 1);
            info.title = core::utf16_to_utf8(title);
        }
    }
    return info;
}

bool Session::in_range(int page) const {
    return m_doc && page >= 0 && page < static_cast<int>(m_sizes.size());
}

std::uint64_t Session::revision(int page) const {
    return in_range(page) ? m_revisions[static_cast<std::size_t>(page)] : 0;
}

Result<FPDF_PAGE> Session::page(int index) {
    if (!in_range(index)) {
        return std::unexpected{"página fuera de rango"};
    }
    auto it = m_pages.find(index);
    if (it == m_pages.end()) {
        // Descargar la página no fijada usada hace más tiempo si hay demasiadas.
        if (m_pages.size() >= max_loaded_pages) {
            auto victim = m_pages.end();
            for (auto p = m_pages.begin(); p != m_pages.end(); ++p) {
                if (!p->second.pinned &&
                    (victim == m_pages.end() || p->second.last_use < victim->second.last_use)) {
                    victim = p;
                }
            }
            if (victim != m_pages.end()) {
                victim->second.text.reset();
                m_pages.erase(victim);
            }
        }
        PagePtr loaded{FPDF_LoadPage(m_doc.get(), index)};
        if (!loaded) {
            return std::unexpected{"no se pudo cargar la página " + std::to_string(index + 1)};
        }
        it = m_pages.emplace(index, LoadedPage{std::move(loaded), nullptr, false, 0}).first;
    }
    it->second.last_use = ++m_use_counter;
    return it->second.page.get();
}

Result<FPDF_TEXTPAGE> Session::text_page(int index) {
    auto p = page(index);
    if (!p) {
        return std::unexpected{p.error()};
    }
    auto& loaded = m_pages.at(index);
    if (!loaded.text) {
        loaded.text.reset(FPDFText_LoadPage(*p));
        if (!loaded.text) {
            return std::unexpected{"no se pudo leer el texto de la página"};
        }
    }
    return loaded.text.get();
}

void Session::mark_edited(int index) {
    auto& loaded = m_pages.at(index);
    // GenerateContent reescribe el flujo de contenido de la página dentro del
    // documento: sin esto, guardar no incluiría los cambios.
    FPDFPage_GenerateContent(loaded.page.get());
    loaded.pinned = true;
    loaded.text.reset(); // el texto cambió: la text page en caché ya no vale
    ++m_revisions[static_cast<std::size_t>(index)];
}

// --- Coordenadas ------------------------------------------------------------

Point Session::to_pdf(FPDF_PAGE page, int index, Point view) const {
    const auto& size = m_sizes[static_cast<std::size_t>(index)];
    const int w = static_cast<int>(std::lround(size.width * coord_precision));
    const int h = static_cast<int>(std::lround(size.height * coord_precision));
    double x = 0.0;
    double y = 0.0;
    FPDF_DeviceToPage(page, 0, 0, w, h, 0, static_cast<int>(std::lround(view.x * coord_precision)),
                      static_cast<int>(std::lround(view.y * coord_precision)), &x, &y);
    return {static_cast<float>(x), static_cast<float>(y)};
}

Point Session::to_view(FPDF_PAGE page, int index, Point pdf) const {
    const auto& size = m_sizes[static_cast<std::size_t>(index)];
    const int w = static_cast<int>(std::lround(size.width * coord_precision));
    const int h = static_cast<int>(std::lround(size.height * coord_precision));
    int x = 0;
    int y = 0;
    FPDF_PageToDevice(page, 0, 0, w, h, 0, pdf.x, pdf.y, &x, &y);
    return {static_cast<float>(x / coord_precision), static_cast<float>(y / coord_precision)};
}

Rect Session::rect_to_view(FPDF_PAGE page, int index, float left, float bottom, float right,
                           float top) const {
    const Point a = to_view(page, index, {left, bottom});
    const Point b = to_view(page, index, {right, top});
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

// --- Lectura ----------------------------------------------------------------

Result<RenderedPage> Session::render(int index, float scale) {
    auto p = page(index);
    if (!p) {
        return std::unexpected{p.error()};
    }
    const auto& size = m_sizes[static_cast<std::size_t>(index)];
    const float longest = std::max(size.width, size.height);
    if (longest * scale > static_cast<float>(max_bitmap_side)) {
        scale = static_cast<float>(max_bitmap_side) / longest;
    }
    const int w = std::max(1, static_cast<int>(std::lround(size.width * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(size.height * scale)));

    BitmapPtr bitmap{FPDFBitmap_Create(w, h, 0)};
    if (!bitmap) {
        return std::unexpected{"sin memoria para renderizar la página"};
    }
    FPDFBitmap_FillRect(bitmap.get(), 0, 0, w, h, 0xFFFFFFFF); // fondo blanco
    FPDF_RenderPageBitmap(bitmap.get(), *p, 0, 0, w, h, 0, FPDF_ANNOT);

    RenderedPage out;
    out.page = index;
    out.scale = scale;
    out.revision = revision(index);
    out.bitmap.width = w;
    out.bitmap.height = h;
    out.bitmap.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    const auto* src = static_cast<const std::uint8_t*>(FPDFBitmap_GetBuffer(bitmap.get()));
    const int stride = FPDFBitmap_GetStride(bitmap.get());
    const std::size_t row_bytes = static_cast<std::size_t>(w) * 4;
    for (int y = 0; y < h; ++y) {
        std::memcpy(out.bitmap.pixels.data() + static_cast<std::size_t>(y) * row_bytes,
                    src + static_cast<std::ptrdiff_t>(y) * stride, row_bytes);
    }
    return out;
}

std::vector<OutlineItem> Session::outline() {
    std::vector<OutlineItem> items;
    if (m_doc) {
        int budget = 10000;
        outline_recursive(m_doc.get(), nullptr, items, 0, budget);
    }
    return items;
}

std::vector<SearchHit> Session::search(const std::string& query, bool match_case, bool whole_word) {
    std::vector<SearchHit> hits;
    if (!m_doc || query.empty()) {
        return hits;
    }
    const std::u16string needle = core::utf8_to_utf16(query);
    unsigned long flags = 0;
    if (match_case) {
        flags |= FPDF_MATCHCASE;
    }
    if (whole_word) {
        flags |= FPDF_MATCHWHOLEWORD;
    }
    for (int i = 0; i < static_cast<int>(m_sizes.size()); ++i) {
        auto text = text_page(i);
        if (!text) {
            continue;
        }
        FPDF_PAGE p = m_pages.at(i).page.get();
        SearchPtr search{FPDFText_FindStart(*text, as_widestring(needle), flags, 0)};
        if (!search) {
            continue;
        }
        while (FPDFText_FindNext(search.get())) {
            const int start = FPDFText_GetSchResultIndex(search.get());
            const int count = FPDFText_GetSchCount(search.get());
            SearchHit hit;
            hit.page = i;
            const int rects = FPDFText_CountRects(*text, start, count);
            for (int r = 0; r < rects; ++r) {
                double l = 0;
                double t = 0;
                double rr = 0;
                double b = 0;
                if (FPDFText_GetRect(*text, r, &l, &t, &rr, &b)) {
                    hit.rects.push_back(rect_to_view(p, i, static_cast<float>(l),
                                                     static_cast<float>(b), static_cast<float>(rr),
                                                     static_cast<float>(t)));
                }
            }
            hits.push_back(std::move(hit));
        }
    }
    return hits;
}

int Session::char_index_at(int index, Point view, float x_tolerance, float y_tolerance) {
    auto text = text_page(index);
    if (!text) {
        return -1;
    }
    const Point pdf = to_pdf(m_pages.at(index).page.get(), index, view);
    const int found = FPDFText_GetCharIndexAtPos(*text, pdf.x, pdf.y, x_tolerance, y_tolerance);
    return found >= 0 ? found : -1;
}

Result<TextSelection> Session::text_range(int index, int from, int to) {
    auto text = text_page(index);
    if (!text) {
        return std::unexpected{text.error()};
    }
    const int total = FPDFText_CountChars(*text);
    if (total <= 0) {
        return TextSelection{index, {}, {}};
    }
    from = std::clamp(from, 0, total - 1);
    to = std::clamp(to, 0, total - 1);
    const int start = std::min(from, to);
    const int count = std::abs(to - from) + 1;

    TextSelection sel;
    sel.page = index;
    FPDF_PAGE p = m_pages.at(index).page.get();
    const int rects = FPDFText_CountRects(*text, start, count);
    for (int r = 0; r < rects; ++r) {
        double l = 0;
        double t = 0;
        double rr = 0;
        double b = 0;
        if (FPDFText_GetRect(*text, r, &l, &t, &rr, &b)) {
            sel.rects.push_back(rect_to_view(p, index, static_cast<float>(l), static_cast<float>(b),
                                             static_cast<float>(rr), static_cast<float>(t)));
        }
    }
    std::u16string buffer(static_cast<std::size_t>(count) + 1, u'\0');
    const int written =
        FPDFText_GetText(*text, start, count, reinterpret_cast<unsigned short*>(buffer.data()));
    buffer.resize(written > 0 ? static_cast<std::size_t>(written - 1) : 0);
    // PDFium usa \r\n entre líneas; normalizamos a \n para el portapapeles.
    std::erase(buffer, u'\r');
    sel.text = core::utf16_to_utf8(buffer);
    return sel;
}

ObjectInfo Session::describe(int index, FPDF_PAGEOBJECT obj) {
    ObjectInfo info;
    info.id = {index, reinterpret_cast<std::uint64_t>(obj)};
    FPDF_PAGE p = m_pages.at(index).page.get();
    float l = 0;
    float b = 0;
    float r = 0;
    float t = 0;
    if (FPDFPageObj_GetBounds(obj, &l, &b, &r, &t)) {
        info.bounds = rect_to_view(p, index, l, b, r, t);
    }
    switch (FPDFPageObj_GetType(obj)) {
    case FPDF_PAGEOBJ_TEXT: {
        info.kind = ObjectKind::text;
        if (auto text = text_page(index)) {
            const unsigned long bytes = FPDFTextObj_GetText(obj, *text, nullptr, 0);
            if (bytes > 2) {
                std::u16string s(bytes / 2, u'\0');
                FPDFTextObj_GetText(obj, *text, reinterpret_cast<FPDF_WCHAR*>(s.data()), bytes);
                s.resize(bytes / 2 - 1);
                info.text = core::utf16_to_utf8(s);
            }
        }
        float size = 0;
        FPDFTextObj_GetFontSize(obj, &size);
        FS_MATRIX m{};
        if (FPDFPageObj_GetMatrix(obj, &m)) {
            size *= static_cast<float>(std::hypot(m.a, m.b));
        }
        info.font_size = size;
        unsigned int cr = 0;
        unsigned int cg = 0;
        unsigned int cb = 0;
        unsigned int ca = 255;
        if (FPDFPageObj_GetFillColor(obj, &cr, &cg, &cb, &ca)) {
            info.color = {static_cast<std::uint8_t>(cr), static_cast<std::uint8_t>(cg),
                          static_cast<std::uint8_t>(cb), static_cast<std::uint8_t>(ca)};
        }
        if (FPDF_FONT font = FPDFTextObj_GetFont(obj); font != nullptr) {
            const std::size_t len = FPDFFont_GetBaseFontName(font, nullptr, 0);
            if (len > 1) {
                std::string name(len, '\0');
                FPDFFont_GetBaseFontName(font, name.data(), len);
                name.resize(len - 1);
                info.font_name = std::move(name);
            }
        }
        break;
    }
    case FPDF_PAGEOBJ_IMAGE:
        info.kind = ObjectKind::image;
        break;
    default:
        info.kind = ObjectKind::other;
        break;
    }
    return info;
}

Result<std::vector<ObjectInfo>> Session::objects(int index) {
    auto p = page(index);
    if (!p) {
        return std::unexpected{p.error()};
    }
    std::vector<ObjectInfo> out;
    const int count = FPDFPage_CountObjects(*p);
    out.reserve(static_cast<std::size_t>(std::max(0, count)));
    for (int i = 0; i < count; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(*p, i);
        const int type = FPDFPageObj_GetType(obj);
        if (type == FPDF_PAGEOBJ_TEXT || type == FPDF_PAGEOBJ_IMAGE) {
            out.push_back(describe(index, obj));
        }
    }
    // Para poder editar objetos existentes sus ObjectId deben seguir vivos:
    // fijamos la página para que no se descargue.
    m_pages.at(index).pinned = true;
    return out;
}

Result<FPDF_PAGEOBJECT> Session::find_object(ObjectId id, int* index_out) {
    auto p = page(id.page);
    if (!p) {
        return std::unexpected{p.error()};
    }
    const int count = FPDFPage_CountObjects(*p);
    for (int i = 0; i < count; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(*p, i);
        if (reinterpret_cast<std::uint64_t>(obj) == id.handle) {
            if (index_out != nullptr) {
                *index_out = i;
            }
            return obj;
        }
    }
    return std::unexpected{"el objeto ya no existe en la página"};
}

Result<ObjectInfo> Session::object_info(ObjectId id) {
    auto obj = find_object(id);
    if (!obj) {
        return std::unexpected{obj.error()};
    }
    return describe(id.page, *obj);
}

// --- Edición ----------------------------------------------------------------

Result<FPDF_FONT> Session::embedded_font() {
    if (m_font) {
        return m_font.get();
    }
    if (m_font_bytes.empty()) {
        std::ifstream in{m_font_path, std::ios::binary};
        if (!in) {
            return std::unexpected{"no se encontró la fuente incrustable"};
        }
        m_font_bytes.assign(std::istreambuf_iterator<char>{in}, {});
    }
    // cid = true: fuente CID con mapeo Unicode, para cualquier carácter de Inter.
    m_font.reset(FPDFText_LoadFont(m_doc.get(), m_font_bytes.data(),
                                   static_cast<std::uint32_t>(m_font_bytes.size()),
                                   FPDF_FONT_TRUETYPE, 1));
    if (!m_font) {
        return std::unexpected{"PDFium no pudo cargar la fuente incrustable"};
    }
    return m_font.get();
}

Result<FPDF_PAGEOBJECT> Session::create_text_object(const TextSpec& spec) {
    if (spec.text.empty()) {
        return std::unexpected{"el texto está vacío"};
    }
    const float size = std::clamp(spec.size, 1.0f, 500.0f);
    FPDF_PAGEOBJECT obj = nullptr;
    if (spec.font == StandardFont::embedded_inter) {
        auto font = embedded_font();
        if (!font) {
            return std::unexpected{font.error()};
        }
        obj = FPDFPageObj_CreateTextObj(m_doc.get(), *font, size);
    } else {
        obj = FPDFPageObj_NewTextObj(m_doc.get(), standard_font_name(spec.font), size);
    }
    if (obj == nullptr) {
        return std::unexpected{"no se pudo crear el objeto de texto"};
    }
    PageObjectPtr guard{obj}; // si algo falla abajo, se destruye solo
    const std::u16string text = core::utf8_to_utf16(spec.text);
    if (!FPDFText_SetText(obj, as_widestring(text))) {
        return std::unexpected{"no se pudo asignar el texto"};
    }
    FPDFPageObj_SetFillColor(obj, spec.color.r, spec.color.g, spec.color.b, spec.color.a);
    return guard.release();
}

Result<ObjectInfo> Session::insert_text(int index, const TextSpec& spec) {
    auto p = page(index);
    if (!p) {
        return std::unexpected{p.error()};
    }
    auto created = create_text_object(spec);
    if (!created) {
        return std::unexpected{created.error()};
    }
    PageObjectPtr obj{*created};

    // Orientación: tomamos los vectores "derecha" y "arriba" de la vista y los
    // pasamos a coordenadas PDF. Así el texto sale derecho aunque la página
    // tenga /Rotate (la matriz absorbe la rotación).
    const Point o = to_pdf(*p, index, spec.origin);
    const Point right = to_pdf(*p, index, {spec.origin.x + 100.0f, spec.origin.y});
    const Point up = to_pdf(*p, index, {spec.origin.x, spec.origin.y - 100.0f});
    const double a = (right.x - o.x) / 100.0;
    const double b = (right.y - o.y) / 100.0;
    const double c = (up.x - o.x) / 100.0;
    const double d = (up.y - o.y) / 100.0;
    FPDFPageObj_Transform(obj.get(), a, b, c, d, o.x, o.y);

    // El origen de un texto PDF es la línea base; el usuario indicó la esquina
    // superior izquierda. Medimos y desplazamos para que coincidan.
    float l = 0;
    float bt = 0;
    float r = 0;
    float t = 0;
    if (FPDFPageObj_GetBounds(obj.get(), &l, &bt, &r, &t)) {
        const Rect view = rect_to_view(*p, index, l, bt, r, t);
        const Point target = to_pdf(*p, index, spec.origin);
        const Point current = to_pdf(*p, index, {view.x0, view.y0});
        FPDFPageObj_Transform(obj.get(), 1, 0, 0, 1, target.x - current.x, target.y - current.y);
    }

    FPDF_PAGEOBJECT raw = obj.release(); // a partir de aquí la página es la dueña
    FPDFPage_InsertObject(*p, raw);
    mark_edited(index);
    return describe(index, raw);
}

Result<ObjectInfo> Session::insert_image(int index, const ImageSpec& spec) {
    auto p = page(index);
    if (!p) {
        return std::unexpected{p.error()};
    }
    const auto& bmp = spec.bitmap;
    if (bmp.width <= 0 || bmp.height <= 0 ||
        bmp.pixels.size() < static_cast<std::size_t>(bmp.width) * bmp.height * 4) {
        return std::unexpected{"imagen vacía o dañada"};
    }
    PageObjectPtr obj{FPDFPageObj_NewImageObj(m_doc.get())};
    if (!obj) {
        return std::unexpected{"no se pudo crear el objeto de imagen"};
    }
    // CreateEx envuelve nuestro buffer sin copiarlo; SetBitmap lo copia al PDF.
    BitmapPtr wrapped{FPDFBitmap_CreateEx(bmp.width, bmp.height, FPDFBitmap_BGRA,
                                          const_cast<std::uint8_t*>(bmp.pixels.data()),
                                          bmp.width * 4)};
    FPDF_PAGE pages[] = {*p};
    if (!wrapped || !FPDFImageObj_SetBitmap(pages, 1, obj.get(), wrapped.get())) {
        return std::unexpected{"no se pudo incrustar la imagen"};
    }
    // Una imagen PDF ocupa el cuadrado unidad; la matriz lo lleva al rectángulo
    // destino. Mapeamos 3 esquinas para soportar páginas rotadas.
    const Rect& rc = spec.rect;
    const Point p0 = to_pdf(*p, index, {rc.x0, rc.y1}); // (0,0) -> abajo-izquierda
    const Point p1 = to_pdf(*p, index, {rc.x1, rc.y1}); // (1,0) -> abajo-derecha
    const Point p2 = to_pdf(*p, index, {rc.x0, rc.y0}); // (0,1) -> arriba-izquierda
    FPDFImageObj_SetMatrix(obj.get(), p1.x - p0.x, p1.y - p0.y, p2.x - p0.x, p2.y - p0.y, p0.x,
                           p0.y);
    FPDF_PAGEOBJECT raw = obj.release();
    FPDFPage_InsertObject(*p, raw);
    mark_edited(index);
    return describe(index, raw);
}

Result<int> Session::detach(ObjectId id) {
    int position = 0;
    auto obj = find_object(id, &position);
    if (!obj) {
        return std::unexpected{obj.error()};
    }
    FPDF_PAGE p = m_pages.at(id.page).page.get();
    if (!FPDFPage_RemoveObject(p, *obj)) {
        return std::unexpected{"no se pudo quitar el objeto"};
    }
    // RemoveObject NO destruye el objeto: pasa a ser nuestro. Lo guardamos para
    // poder reinsertarlo con deshacer/rehacer conservando el mismo ObjectId.
    m_detached.emplace_back(*obj);
    mark_edited(id.page);
    return position;
}

Result<ObjectInfo> Session::attach(ObjectId id, int index) {
    auto it = std::ranges::find_if(m_detached, [&](const PageObjectPtr& o) {
        return reinterpret_cast<std::uint64_t>(o.get()) == id.handle;
    });
    if (it == m_detached.end()) {
        return std::unexpected{"el objeto no está pendiente de reinsertar"};
    }
    auto p = page(id.page);
    if (!p) {
        return std::unexpected{p.error()};
    }
    const int count = FPDFPage_CountObjects(*p);
    const auto position = static_cast<std::size_t>(std::clamp(index, 0, count));
    FPDF_PAGEOBJECT raw = it->get();
    if (!FPDFPage_InsertObjectAtIndex(*p, raw, position)) {
        return std::unexpected{"no se pudo reinsertar el objeto"};
    }
    static_cast<void>(it->release()); // la página vuelve a ser la dueña
    m_detached.erase(it);
    mark_edited(id.page);
    return describe(id.page, raw);
}

Result<ObjectInfo> Session::set_bounds(ObjectId id, Rect target) {
    auto obj = find_object(id);
    if (!obj) {
        return std::unexpected{obj.error()};
    }
    FPDF_PAGE p = m_pages.at(id.page).page.get();
    const Rect current = describe(id.page, *obj).bounds;
    if (current.width() < 0.01f || current.height() < 0.01f || target.width() < 1.0f ||
        target.height() < 1.0f) {
        return std::unexpected{"tamaño no válido"};
    }
    // Buscamos la transformación afín T (en coordenadas PDF) que lleva el
    // rectángulo actual al nuevo: resolvemos con 3 esquinas de cada uno.
    const Point o0 = to_pdf(p, id.page, {current.x0, current.y0});
    const Point o1 = to_pdf(p, id.page, {current.x1, current.y0});
    const Point o2 = to_pdf(p, id.page, {current.x0, current.y1});
    const Point n0 = to_pdf(p, id.page, {target.x0, target.y0});
    const Point n1 = to_pdf(p, id.page, {target.x1, target.y0});
    const Point n2 = to_pdf(p, id.page, {target.x0, target.y1});
    const double e1x = o1.x - o0.x;
    const double e1y = o1.y - o0.y;
    const double e2x = o2.x - o0.x;
    const double e2y = o2.y - o0.y;
    const double f1x = n1.x - n0.x;
    const double f1y = n1.y - n0.y;
    const double f2x = n2.x - n0.x;
    const double f2y = n2.y - n0.y;
    const double det = e1x * e2y - e2x * e1y;
    if (std::abs(det) < 1e-9) {
        return std::unexpected{"transformación degenerada"};
    }
    // L = [f1 f2] * inv([e1 e2])
    const double inv00 = e2y / det;
    const double inv01 = -e2x / det;
    const double inv10 = -e1y / det;
    const double inv11 = e1x / det;
    const double a = f1x * inv00 + f2x * inv10;
    const double c = f1x * inv01 + f2x * inv11;
    const double b = f1y * inv00 + f2y * inv10;
    const double d = f1y * inv01 + f2y * inv11;
    const double e = n0.x - (a * o0.x + c * o0.y);
    const double f = n0.y - (b * o0.x + d * o0.y);
    FPDFPageObj_Transform(*obj, a, b, c, d, e, f);
    mark_edited(id.page);
    return describe(id.page, *obj);
}

Result<ReplaceResult> Session::replace_text(ObjectId id, const TextSpec& spec) {
    int position = 0;
    auto old_obj = find_object(id, &position);
    if (!old_obj) {
        return std::unexpected{old_obj.error()};
    }
    if (FPDFPageObj_GetType(*old_obj) != FPDF_PAGEOBJ_TEXT) {
        return std::unexpected{"el objeto seleccionado no es texto"};
    }
    FS_MATRIX m{};
    if (!FPDFPageObj_GetMatrix(*old_obj, &m)) {
        return std::unexpected{"no se pudo leer la posición del texto"};
    }
    auto created = create_text_object(spec);
    if (!created) {
        return std::unexpected{created.error()};
    }
    PageObjectPtr fresh{*created};
    // Conservamos origen (línea base) y orientación del original; el tamaño lo
    // da spec.size, así que normalizamos la escala de la matriz.
    const double k = std::hypot(m.a, m.b);
    if (k > 1e-6) {
        FPDFPageObj_Transform(fresh.get(), m.a / k, m.b / k, m.c / k, m.d / k, m.e, m.f);
    } else {
        FPDFPageObj_Transform(fresh.get(), 1, 0, 0, 1, m.e, m.f);
    }

    FPDF_PAGE p = m_pages.at(id.page).page.get();
    if (!FPDFPage_RemoveObject(p, *old_obj)) {
        return std::unexpected{"no se pudo quitar el texto original"};
    }
    m_detached.emplace_back(*old_obj);
    FPDF_PAGEOBJECT raw = fresh.release();
    FPDFPage_InsertObjectAtIndex(p, raw, static_cast<std::size_t>(position));
    mark_edited(id.page);
    return ReplaceResult{position, describe(id.page, raw)};
}

// --- Anotaciones ------------------------------------------------------------

Result<int> Session::add_annotation(int index, const AnnotSpec& spec) {
    auto p = page(index);
    if (!p) {
        return std::unexpected{p.error()};
    }
    FPDF_ANNOTATION_SUBTYPE subtype = FPDF_ANNOT_HIGHLIGHT;
    switch (spec.kind) {
    case AnnotKind::highlight:
        subtype = FPDF_ANNOT_HIGHLIGHT;
        break;
    case AnnotKind::underline:
        subtype = FPDF_ANNOT_UNDERLINE;
        break;
    case AnnotKind::strikeout:
        subtype = FPDF_ANNOT_STRIKEOUT;
        break;
    case AnnotKind::note:
        subtype = FPDF_ANNOT_TEXT;
        break;
    case AnnotKind::ink:
        subtype = FPDF_ANNOT_INK;
        break;
    }
    AnnotPtr annot{FPDFPage_CreateAnnot(*p, subtype)};
    if (!annot) {
        return std::unexpected{"no se pudo crear la anotación"};
    }
    FPDFAnnot_SetFlags(annot.get(), FPDF_ANNOT_FLAG_PRINT);
    FPDFAnnot_SetColor(annot.get(), FPDFANNOT_COLORTYPE_Color, spec.color.r, spec.color.g,
                       spec.color.b, spec.color.a);

    float min_x = 1e9f;
    float min_y = 1e9f;
    float max_x = -1e9f;
    float max_y = -1e9f;
    auto grow = [&](Point q) {
        min_x = std::min(min_x, q.x);
        min_y = std::min(min_y, q.y);
        max_x = std::max(max_x, q.x);
        max_y = std::max(max_y, q.y);
    };

    if (spec.kind == AnnotKind::note) {
        const Point q = to_pdf(*p, index, spec.position);
        grow(q);
        grow({q.x + 20.0f, q.y - 20.0f});
        const std::u16string contents = core::utf8_to_utf16(spec.contents);
        FPDFAnnot_SetStringValue(annot.get(), "Contents", as_widestring(contents));
    } else if (spec.kind == AnnotKind::ink) {
        FPDFAnnot_SetBorder(annot.get(), 0.0f, 0.0f, 2.0f);
        for (const auto& stroke : spec.strokes) {
            std::vector<FS_POINTF> points;
            points.reserve(stroke.size());
            for (const Point& v : stroke) {
                const Point q = to_pdf(*p, index, v);
                grow(q);
                points.push_back({q.x, q.y});
            }
            if (points.size() >= 2) {
                FPDFAnnot_AddInkStroke(annot.get(), points.data(), points.size());
            }
        }
        min_x -= 2.0f;
        min_y -= 2.0f;
        max_x += 2.0f;
        max_y += 2.0f;
    } else {
        for (const Rect& r : spec.rects) {
            // Orden de QuadPoints que usan Acrobat y PDFium: arriba-izq, arriba-der,
            // abajo-izq, abajo-der.
            const Point tl = to_pdf(*p, index, {r.x0, r.y0});
            const Point tr = to_pdf(*p, index, {r.x1, r.y0});
            const Point bl = to_pdf(*p, index, {r.x0, r.y1});
            const Point br = to_pdf(*p, index, {r.x1, r.y1});
            const FS_QUADPOINTSF quad{tl.x, tl.y, tr.x, tr.y, bl.x, bl.y, br.x, br.y};
            FPDFAnnot_AppendAttachmentPoints(annot.get(), &quad);
            grow(tl);
            grow(tr);
            grow(bl);
            grow(br);
        }
    }
    if (min_x > max_x) {
        return std::unexpected{"anotación vacía"};
    }
    const FS_RECTF rect{min_x, max_y, max_x, min_y}; // left, top, right, bottom
    FPDFAnnot_SetRect(annot.get(), &rect);
    annot.reset();
    mark_edited(index);
    return FPDFPage_GetAnnotCount(*p) - 1;
}

Result<void> Session::remove_annotation(int index, int annot_index) {
    auto p = page(index);
    if (!p) {
        return std::unexpected{p.error()};
    }
    if (!FPDFPage_RemoveAnnot(*p, annot_index)) {
        return std::unexpected{"no se pudo quitar la anotación"};
    }
    mark_edited(index);
    return {};
}

// --- Páginas ----------------------------------------------------------------

Result<DocInfo> Session::rotate_page(int index, int quarter_turns) {
    auto p = page(index);
    if (!p) {
        return std::unexpected{p.error()};
    }
    const int rotation = ((FPDFPage_GetRotation(*p) + quarter_turns) % 4 + 4) % 4;
    FPDFPage_SetRotation(*p, rotation);
    m_sizes[static_cast<std::size_t>(index)] = {FPDF_GetPageWidthF(*p), FPDF_GetPageHeightF(*p)};
    mark_edited(index);
    return make_info();
}

Result<DocInfo> Session::delete_pages(std::vector<int> pages) {
    if (!m_doc) {
        return std::unexpected{"no hay documento abierto"};
    }
    std::ranges::sort(pages, std::greater<>{});
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    if (pages.size() >= m_sizes.size()) {
        return std::unexpected{"un PDF debe conservar al menos una página"};
    }
    // Cambia la numeración: cerramos todas las páginas cargadas (su contenido
    // editado ya está escrito en el documento por GenerateContent).
    unload_pages();
    destroy_detached();
    for (int index : pages) {
        if (in_range(index)) {
            FPDFPage_Delete(m_doc.get(), index);
            m_sizes.erase(m_sizes.begin() + index);
            m_revisions.erase(m_revisions.begin() + index);
        }
    }
    return make_info();
}

Result<DocInfo> Session::move_page(int from, int to) {
    if (!in_range(from) || !in_range(to)) {
        return std::unexpected{"página fuera de rango"};
    }
    if (from == to) {
        return make_info();
    }
    unload_pages();
    destroy_detached();
    const int indices[] = {from};
    if (!FPDF_MovePages(m_doc.get(), indices, 1, to)) {
        return std::unexpected{"no se pudo mover la página"};
    }
    auto size = m_sizes[static_cast<std::size_t>(from)];
    m_sizes.erase(m_sizes.begin() + from);
    m_sizes.insert(m_sizes.begin() + to, size);
    for (auto& r : m_revisions) {
        ++r; // las páginas cambian de posición: todo el renderizado en caché caduca
    }
    return make_info();
}

Result<DocInfo> Session::import_pages(const std::filesystem::path& source, int at_index) {
    if (!m_doc) {
        return std::unexpected{"no hay documento abierto"};
    }
    auto reader = FileReader::open(source);
    if (!reader) {
        return std::unexpected{reader.error()};
    }
    DocumentPtr src{FPDF_LoadCustomDocument((*reader)->access(), nullptr)};
    if (!src) {
        return std::unexpected{last_error_message()};
    }
    at_index = std::clamp(at_index, 0, static_cast<int>(m_sizes.size()));
    unload_pages();
    destroy_detached();
    if (!FPDF_ImportPages(m_doc.get(), src.get(), nullptr, at_index)) {
        return std::unexpected{"no se pudieron importar las páginas"};
    }
    const int count = FPDF_GetPageCount(m_doc.get());
    m_sizes.assign(static_cast<std::size_t>(count), PageSize{});
    for (int i = 0; i < count; ++i) {
        FS_SIZEF size{};
        FPDF_GetPageSizeByIndexF(m_doc.get(), i, &size);
        m_sizes[static_cast<std::size_t>(i)] = {size.width, size.height};
    }
    std::vector<std::uint64_t> revisions(m_sizes.size(), 0);
    for (std::size_t i = 0; i < revisions.size(); ++i) {
        revisions[i] = (i < m_revisions.size() ? m_revisions[i] : 0) + 1;
    }
    m_revisions = std::move(revisions);
    return make_info();
}

Result<void> Session::extract_pages(const std::vector<int>& pages,
                                    const std::filesystem::path& dest) {
    if (!m_doc || pages.empty()) {
        return std::unexpected{"no hay páginas para extraer"};
    }
    if (same_path(dest, m_source)) {
        return std::unexpected{"elige un archivo distinto del documento abierto"};
    }
    DocumentPtr out{FPDF_CreateNewDocument()};
    if (!out || !FPDF_ImportPagesByIndex(out.get(), m_doc.get(), pages.data(),
                                         static_cast<unsigned long>(pages.size()), 0)) {
        return std::unexpected{"no se pudieron copiar las páginas"};
    }
    return save_document(out.get(), dest);
}

Result<SaveResult> Session::save(const std::filesystem::path& dest) {
    if (!m_doc) {
        return std::unexpected{"no hay documento abierto"};
    }
    // Siempre escribimos a un temporal en la misma carpeta y renombramos al
    // final: si algo falla a medias, el archivo original queda intacto.
    auto tmp = dest;
    tmp += ".pdflite-tmp";
    if (auto saved = save_document(m_doc.get(), tmp); !saved) {
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        return std::unexpected{saved.error()};
    }

    SaveResult result;
    if (same_path(dest, m_source)) {
        // PDFium sigue leyendo del archivo original bajo demanda: hay que cerrar
        // el documento antes de reemplazarlo y volver a abrirlo después.
        close();
        std::error_code ec;
        std::filesystem::rename(tmp, dest, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            static_cast<void>(open(dest)); // el original sigue intacto: lo reabrimos
            return std::unexpected{"no se pudo reemplazar el archivo (¿está abierto en otro "
                                   "programa?)"};
        }
        auto reopened = open(dest);
        if (!reopened) {
            return std::unexpected{"guardado, pero no se pudo reabrir: " + reopened.error()};
        }
        result.info = std::move(*reopened);
        result.reopened = true;
        return result;
    }

    std::error_code ec;
    std::filesystem::rename(tmp, dest, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return std::unexpected{"no se pudo escribir el archivo de destino"};
    }
    m_path = dest;
    result.info = make_info();
    return result;
}

Result<void> Session::create_blank(const std::filesystem::path& dest, int pages, float width,
                                   float height) {
    DocumentPtr doc{FPDF_CreateNewDocument()};
    if (!doc) {
        return std::unexpected{"no se pudo crear el documento"};
    }
    for (int i = 0; i < pages; ++i) {
        PagePtr page{FPDFPage_New(doc.get(), i, width, height)};
        if (!page) {
            return std::unexpected{"no se pudo crear la página"};
        }
    }
    return save_document(doc.get(), dest);
}

} // namespace pdf::internal
