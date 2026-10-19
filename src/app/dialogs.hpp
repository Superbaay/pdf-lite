#pragma once

#include <filesystem>
#include <functional>

struct SDL_Window;

namespace app {

enum class DialogKind { open_pdf, save_pdf, open_image };

// Muestra un diálogo nativo de archivo (SDL3). `on_chosen` puede llamarse
// DESDE OTRO HILO y solo si el usuario eligió un archivo (no al cancelar):
// quien llama debe reenviar el resultado al hilo de UI.
void show_file_dialog(SDL_Window* window, DialogKind kind,
                      const std::filesystem::path& default_location,
                      std::function<void(std::filesystem::path)> on_chosen);

} // namespace app
