# Changelog

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y
[versionado semántico](https://semver.org/lang/es/).

## [Sin publicar]

### Añadido
- Fase 0: estructura del proyecto, CMake + presets + vcpkg (manifiesto, baseline fijada).
- Descarga de PDFium `chromium/8076` verificada con SHA-256.
- Ventana SDL3 + Dear ImGui (SDL_Renderer) con bucle dirigido por eventos.
- Fuentes Inter + iconos Lucide empaquetados.
- Envoltorios RAII sobre PDFium y pruebas con doctest/CTest.
- clang-format, clang-tidy, CI de GitHub Actions (MSVC y clang-cl).
- Configuración de VS Code (F5 compila y depura).

### Cambiado
- Estándar C++23 en lugar de C++20 para usar `std::expected` (ADR 0005).
