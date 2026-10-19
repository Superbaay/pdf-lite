// SDL_main.h define WinMain en Windows y nos entrega argv en UTF-8.
#include "app/app.hpp"

#include <SDL3/SDL_main.h>

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "pdf-lite", SDL_GetError(), nullptr);
        return 1;
    }

    int exit_code = 1;
    {
        // Ámbito propio: App (ventana, renderer, ImGui) se destruye ANTES de SDL_Quit.
        auto app = app::App::create(argc, argv);
        if (app) {
            exit_code = (*app)->run();
        } else {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "pdf-lite", app.error().c_str(),
                                     nullptr);
        }
    }
    SDL_Quit();
    return exit_code;
}
