# Dependencias

Todas las versiones están fijadas. Las de vcpkg se fijan con `builtin-baseline`
(`fdd4e6059c803f56bb5df86e285bf374d9d44407`, 2026-09-30) en `vcpkg.json`; PDFium con tag
+ SHA-256 en `cmake/Pdfium.cmake`; las fuentes van versionadas en `assets/fonts/`.

| Nombre | Versión | Licencia | Tamaño aprox. en el distribuible | Motivo |
|---|---|---|---|---|
| [PDFium](https://pdfium.googlesource.com/pdfium/) vía [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries) | chromium/8076 (156.0.8076.0), build sin V8/XFA | PDFium: BSD-3-Clause + Apache-2.0; empaquetado: MIT; librerías internas en `licenses/` del paquete (FreeType FTL, libjpeg-turbo IJG/BSD, lcms MIT, ICU, zlib, abseil Apache-2.0…) | `pdfium.dll` ≈ 7,0 MB | Motor PDF de Chrome: renderizado, texto, edición y guardado. Precompilado para evitar depot_tools/GN. |
| [SDL3](https://github.com/libsdl-org/SDL) | 3.4.16 | Zlib | estático, ≈ 1–2 MB dentro del exe | Ventana, entrada, arrastrar y soltar, diálogos de archivo nativos, renderer (D3D11). |
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.92.9 | MIT | estático, ≈ 0,5–1 MB | Interfaz en modo inmediato; muy ligera, sin dependencias. |
| [stb](https://github.com/nothings/stb) (`stb_image.h`) | 2024-07-29 | MIT o dominio público | solo headers, ≈ 50 KB | Cargar PNG/JPG para insertar imágenes (Fase 3). |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | solo headers | Configuración y archivos recientes (Fase 1). Rutas Unicode y escapes correctos sin escribir un parser propio. |
| [doctest](https://github.com/doctest/doctest) | 2.5.3 | MIT | solo pruebas, no se distribuye | Framework de pruebas muy rápido de compilar. |
| [Inter](https://github.com/rsms/inter) | 4.1 (`Inter-Regular.ttf`, `Inter-SemiBold.ttf`) | **SIL OFL-1.1** | ≈ 0,8 MB | Tipografía de la interfaz con cobertura completa del español. |
| [Lucide](https://github.com/lucide-icons/lucide) | 1.52.0 (`lucide.ttf`) | ISC (y MIT para los iconos heredados de Feather) | ≈ 0,9 MB | Iconos como fuente: se fusionan en la fuente de ImGui. |

## Notas de licencia

- **OFL-1.1 (Inter)** no está en la lista inicial (MIT/BSD/Apache/zlib/BSL/MPL/dominio
  público), pero el prompt del proyecto propone Inter explícitamente. La OFL permite usar,
  empaquetar y redistribuir la fuente con software de cualquier licencia; solo prohíbe venderla
  sola y exige conservar su licencia (`assets/fonts/Inter-LICENSE.txt`). No es copyleft para
  el código.
- **ISC (Lucide)** es equivalente a MIT.
- El paquete de PDFium incluye las licencias de sus componentes internos; se copiarán al
  distribuible en la fase de empaquetado.

## Herramientas (no se distribuyen)

| Herramienta | Versión probada | Licencia |
|---|---|---|
| MSVC (VS 2022 Build Tools) | 14.44 | Propietaria (solo compilación) |
| clang-cl / clang-format (LLVM de VS) | la que trae VS | Apache-2.0 con excepción LLVM |
| CMake | 4.4.4 | BSD-3-Clause |
| Ninja | 1.13.2 | Apache-2.0 |
| vcpkg | 2026-09-26 | MIT |
