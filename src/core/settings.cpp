#include "core/settings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

namespace core {

namespace {

// Las rutas se guardan en UTF-8 para que acentos y ñ sobrevivan en JSON.
std::string path_to_utf8(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return {u8.begin(), u8.end()};
}

std::filesystem::path utf8_to_path(const std::string& s) {
    return std::filesystem::path{std::u8string{s.begin(), s.end()}};
}

bool same_file(const std::filesystem::path& a, const std::filesystem::path& b) {
    // Comparación sin distinguir mayúsculas: en Windows C:\A.pdf y c:\a.PDF son el mismo.
    auto norm = [](const std::filesystem::path& p) {
        auto s = p.lexically_normal().wstring();
        // towlower con el locale "C" no convierte Á/Ñ/Ó: plegamos Latin-1 a mano.
        std::ranges::transform(s, s.begin(), [](wchar_t c) {
            if (c >= 0xC0 && c <= 0xDE && c != 0xD7) {
                return static_cast<wchar_t>(c + 0x20);
            }
            return static_cast<wchar_t>(std::towlower(c));
        });
        return s;
    };
    return norm(a) == norm(b);
}

} // namespace

void Settings::touch_recent(const std::filesystem::path& path, int last_page, float zoom) {
    remove_recent(path);
    recents.insert(recents.begin(), RecentFile{path, last_page, zoom});
    if (recents.size() > max_recents) {
        recents.resize(max_recents);
    }
}

const RecentFile* Settings::find_recent(const std::filesystem::path& path) const {
    auto it =
        std::ranges::find_if(recents, [&](const RecentFile& r) { return same_file(r.path, path); });
    return it == recents.end() ? nullptr : &*it;
}

void Settings::remove_recent(const std::filesystem::path& path) {
    std::erase_if(recents, [&](const RecentFile& r) { return same_file(r.path, path); });
}

std::string to_json(const Settings& settings) {
    nlohmann::json j;
    j["version"] = 1;
    j["theme"] = settings.theme == ThemeMode::light ? "light" : "dark";
    j["sidebar_visible"] = settings.sidebar_visible;
    auto& recents = j["recents"] = nlohmann::json::array();
    for (const auto& r : settings.recents) {
        recents.push_back(
            {{"path", path_to_utf8(r.path)}, {"last_page", r.last_page}, {"zoom", r.zoom}});
    }
    return j.dump(2);
}

Settings from_json(const std::string& text) {
    Settings s;
    // allow_exceptions = false: un JSON corrupto no debe tumbar la app.
    const auto j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object()) {
        return s;
    }
    if (auto it = j.find("theme"); it != j.end() && it->is_string()) {
        s.theme = it->get<std::string>() == "light" ? ThemeMode::light : ThemeMode::dark;
    }
    if (auto it = j.find("sidebar_visible"); it != j.end() && it->is_boolean()) {
        s.sidebar_visible = it->get<bool>();
    }
    if (auto it = j.find("recents"); it != j.end() && it->is_array()) {
        for (const auto& r : *it) {
            if (!r.is_object() || !r.contains("path") || !r["path"].is_string()) {
                continue;
            }
            RecentFile rf;
            rf.path = utf8_to_path(r["path"].get<std::string>());
            if (r.contains("last_page") && r["last_page"].is_number_integer()) {
                rf.last_page = std::max(0, r["last_page"].get<int>());
            }
            if (r.contains("zoom") && r["zoom"].is_number()) {
                rf.zoom = std::clamp(r["zoom"].get<float>(), 0.0f, 16.0f);
            }
            s.recents.push_back(std::move(rf));
            if (s.recents.size() >= Settings::max_recents) {
                break;
            }
        }
    }
    return s;
}

std::expected<Settings, std::string> load_settings(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        return std::unexpected{"no existe el archivo de configuración"};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return from_json(buffer.str());
}

std::expected<void, std::string> save_settings(const Settings& settings,
                                               const std::filesystem::path& file) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    // Escritura atómica: primero a .tmp y luego renombrar, para no dejar un
    // JSON a medias si la app se cierra durante la escritura.
    auto tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out{tmp, std::ios::binary | std::ios::trunc};
        if (!out) {
            return std::unexpected{"no se pudo escribir la configuración"};
        }
        out << to_json(settings);
        if (!out) {
            return std::unexpected{"error al escribir la configuración"};
        }
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        return std::unexpected{"no se pudo reemplazar la configuración: " + ec.message()};
    }
    return {};
}

} // namespace core
