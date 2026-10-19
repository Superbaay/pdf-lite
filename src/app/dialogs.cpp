#include "app/dialogs.hpp"

#include "core/utf.hpp"

#include <SDL3/SDL_dialog.h>

#include <memory>
#include <string>

namespace app {

namespace {

struct Request {
    std::function<void(std::filesystem::path)> on_chosen;
};

void SDLCALL dialog_callback(void* userdata, const char* const* filelist, int /*filter*/) {
    // Recuperamos la propiedad del Request que cedimos a SDL (ver abajo): se
    // libera al salir de esta función pase lo que pase.
    std::unique_ptr<Request> request{static_cast<Request*>(userdata)};
    if (filelist == nullptr || filelist[0] == nullptr) {
        return; // error o cancelado
    }
    const std::string utf8{filelist[0]};
    request->on_chosen(core::path_from_utf8(utf8));
}

constexpr SDL_DialogFileFilter pdf_filter[] = {{"Documentos PDF", "pdf"}};
constexpr SDL_DialogFileFilter image_filter[] = {{"Imágenes (PNG, JPG, BMP)", "png;jpg;jpeg;bmp"}};

} // namespace

void show_file_dialog(SDL_Window* window, DialogKind kind,
                      const std::filesystem::path& default_location,
                      std::function<void(std::filesystem::path)> on_chosen) {
    // La API de SDL es de C: recibe un void* "userdata". Le cedemos la
    // propiedad de un Request (release) y dialog_callback la recupera.
    auto request = std::make_unique<Request>(Request{std::move(on_chosen)});
    const std::string location = core::path_to_utf8(default_location);
    const char* loc = location.empty() ? nullptr : location.c_str();
    switch (kind) {
    case DialogKind::open_pdf:
        SDL_ShowOpenFileDialog(dialog_callback, request.release(), window, pdf_filter, 1, loc,
                               false);
        break;
    case DialogKind::save_pdf:
        SDL_ShowSaveFileDialog(dialog_callback, request.release(), window, pdf_filter, 1, loc);
        break;
    case DialogKind::open_image:
        SDL_ShowOpenFileDialog(dialog_callback, request.release(), window, image_filter, 1, loc,
                               false);
        break;
    }
}

} // namespace app
