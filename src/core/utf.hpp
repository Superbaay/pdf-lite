#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace core {

// Conversión UTF-8 <-> UTF-16LE (lo que PDFium llama FPDF_WIDESTRING).
// Implementación propia de ~40 líneas: no justifica una dependencia.
// Las secuencias inválidas se reemplazan por U+FFFD en vez de fallar.

inline std::u16string utf8_to_utf16(std::string_view in) {
    std::u16string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size();) {
        const auto c = static_cast<unsigned char>(in[i]);
        char32_t cp = 0xFFFD;
        std::size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6 && i + 1 < in.size()) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(in[i + 1]) & 0x3Fu);
            len = 2;
        } else if ((c >> 4) == 0xE && i + 2 < in.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(in[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(in[i + 2]) & 0x3Fu);
            len = 3;
        } else if ((c >> 3) == 0x1E && i + 3 < in.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(in[i + 1]) & 0x3Fu) << 12) |
                 ((static_cast<unsigned char>(in[i + 2]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(in[i + 3]) & 0x3Fu);
            len = 4;
        }
        i += len;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

inline std::string utf16_to_utf8(std::u16string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        char32_t cp = in[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < in.size() && in[i + 1] >= 0xDC00 &&
            in[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (in[i + 1] - 0xDC00);
            ++i;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD; // sustituto suelto
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// Rutas <-> UTF-8. OJO: en Windows, std::filesystem::path{std::string} interpreta
// los bytes en la página de códigos ANSI, NO en UTF-8: una carpeta con "ñ" se
// convierte en basura. SDL y ImGui hablan UTF-8, así que siempre pasamos por aquí.
inline std::filesystem::path path_from_utf8(std::string_view utf8) {
    return std::filesystem::path{std::u8string{utf8.begin(), utf8.end()}};
}

inline std::string path_to_utf8(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    return {u8.begin(), u8.end()};
}

} // namespace core
