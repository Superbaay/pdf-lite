#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace core {

enum class ThemeMode { dark, light };

struct RecentFile {
    std::filesystem::path path;
    int last_page = 0;
    float zoom = 0.0f; // 0 = ajustar al ancho
};

// Preferencias persistentes del usuario (JSON en la carpeta de datos de la app).
struct Settings {
    ThemeMode theme = ThemeMode::dark;
    bool sidebar_visible = true;
    std::vector<RecentFile> recents; // el más reciente primero

    static constexpr std::size_t max_recents = 12;

    // Mueve `path` al principio de la lista (o lo añade) con su última página.
    void touch_recent(const std::filesystem::path& path, int last_page, float zoom);
    [[nodiscard]] const RecentFile* find_recent(const std::filesystem::path& path) const;
    void remove_recent(const std::filesystem::path& path);
};

std::string to_json(const Settings& settings);
// Tolerante: campos ausentes o con tipo incorrecto toman su valor por defecto.
Settings from_json(const std::string& text);

std::expected<Settings, std::string> load_settings(const std::filesystem::path& file);
std::expected<void, std::string> save_settings(const Settings& settings,
                                               const std::filesystem::path& file);

} // namespace core
