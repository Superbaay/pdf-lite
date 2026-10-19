# pdf-lite

Visor y editor de PDF **100 % local**, de código abierto y lo más ligero posible, escrito en
C++23 ([por qué no C++20](docs/adr/0005-cpp23.md)) sobre
[PDFium](https://pdfium.googlesource.com/pdfium/), [SDL3](https://libsdl.org) y
[Dear ImGui](https://github.com/ocornut/imgui).

- **Ligero**: carpeta distribuible de ~12 MB, ~43 MB de RAM con un PDF de 320 páginas,
  **0 % de CPU en reposo** (la interfaz solo se redibuja cuando pasa algo). Ver
  [docs/BENCHMARKS.md](docs/BENCHMARKS.md).
- **Privado**: sin nube, sin telemetría, sin actualizaciones remotas. Funciona sin internet.
- **Modular**: PDFium aislado en `src/pdf/` (un único hilo), funciones como módulos.

> Versión actual: **0.6.0** — fases 0 a 6 completas. Ver [docs/ROADMAP.md](docs/ROADMAP.md).

## Qué hace

| Área | Funciones |
|---|---|
| Visor | Abrir (diálogo, arrastrar y soltar, línea de comandos), scroll continuo, zoom (Ctrl+rueda anclado al cursor, ajustar ancho/página, 100 %), miniaturas, ir a página, tema claro/oscuro, recientes y última página/zoom de cada archivo |
| Navegación | Búsqueda con resaltado (mayúsculas, palabra completa), índice/marcadores del PDF, selección y copia de texto |
| Edición nivel 1 | Insertar texto (fuentes estándar o Inter incrustada para cualquier carácter Unicode) e imágenes PNG/JPG/BMP; mover, redimensionar, borrar |
| Edición nivel 2 | Reemplazar el texto de un objeto existente (texto, fuente, tamaño, color) |
| Historial | Deshacer/rehacer (patrón Command), guardar y guardar como (siempre copia completa) |
| Páginas | Rotar (deshacible), eliminar, reordenar, unir (añadir otro PDF), dividir (extraer rango) |
| Anotaciones | Resaltado, subrayado, tachado, notas y dibujo libre (anotaciones PDF estándar) |

Atajos: pulsa **F1** dentro de la app.

## Limitación importante: editar un PDF no es como editar un .docx

Un PDF no guarda párrafos: guarda **objetos de texto con glifos en posiciones fijas**. No hay
reflujo: si un texto nuevo es más largo, no empuja al resto. Por eso la edición va por niveles:

| Nivel | Qué hace | Estado |
|---|---|---|
| 1 — Inserción | Crear texto (fuente estándar o TTF incrustada) e imágenes nuevas; mover, redimensionar, borrar. Para cambiar fuente/tamaño/color de un texto se usa el nivel 2. | ✅ v0.3.0 |
| 2 — Reemplazo | Seleccionar un objeto de texto existente, quitarlo del contenido de la página, insertar el nuevo en su posición (misma línea base y orientación), regenerar el contenido y guardar como **copia completa** (no incremental) para que el texto original no quede escondido en el archivo. **El reemplazo es por objeto/línea, sin reflujo.** | ✅ v0.4.0 |
| 3 — Redacción, formularios, firma | Solo investigación y plan: [docs/FASE7.md](docs/FASE7.md). | 📋 |

Otras limitaciones conocidas:

- Las 14 fuentes estándar de PDF solo cubren Latin-1 (español completo incluido). Si el texto
  tiene otros caracteres, la app usa automáticamente Inter incrustada.
- La transparencia de los PNG se aplana sobre blanco al insertarlos.
- La selección de texto funciona dentro de una página (no cruza páginas).
- Eliminar, mover o unir páginas vacía el historial de deshacer (la app avisa antes).
  Rotar sí se puede deshacer. Ver [ADR 0007](docs/adr/0007-operaciones-de-pagina-e-historial.md).
- A zoom muy alto, una página se renderiza como máximo a 8192 px de lado.

## Requisitos para compilar (Windows 10/11 x64)

| Herramienta | Cómo instalarla |
|---|---|
| Visual Studio 2022 Build Tools (MSVC + SDK + clang-cl) | `winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset"` |
| CMake ≥ 3.25 | `winget install Kitware.CMake` |
| Ninja | `winget install Ninja-build.Ninja` |
| Git | `winget install Git.Git` |
| VS Code + extensiones recomendadas | Se sugieren al abrir la carpeta (`.vscode/extensions.json`) |

## Compilar

```powershell
git clone https://github.com/Superbaay/pdf-lite.git
cd pdf-lite
./scripts/bootstrap.ps1          # clona vcpkg DENTRO del repo (ignorado por git), en el commit fijado
```

### Desde VS Code (recomendado)

1. Abre la carpeta `pdf-lite` en VS Code e instala las extensiones recomendadas.
2. CMake Tools pedirá un *preset*: elige **MSVC Debug (+ASan)**.
3. Pulsa **F5**: compila y lanza `pdf_lite.exe` con el depurador. La configuración
   "pdf-lite (abrir un PDF)" pide una ruta y la pasa por línea de comandos.
4. Pruebas: panel *Testing* o paleta de comandos → *CMake: Run Tests*.

La primera configuración tarda unos minutos porque vcpkg compila SDL3, ImGui, etc. Las
siguientes usan la caché en `.vcpkg-cache/` (dentro del repo).

### Desde la terminal ("Developer PowerShell for VS 2022")

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release
ctest --preset msvc-release
./build/msvc-release/src/pdf_lite.exe documento.pdf
```

Presets: `msvc-debug` (con AddressSanitizer), `msvc-release`, `clang-debug`, `clang-release`.

**Distribuible**: `cmake --install build/msvc-release` deja en `out/msvc-release/` el exe,
`pdfium.dll`, `assets/` y `licenses/`. Esa carpeta se puede copiar a cualquier PC con
Windows 10/11 x64 (necesita el *Visual C++ Redistributable*, presente en casi todos los equipos).

## Pruebas

- `pdflite_tests`: núcleo (LRU, colas, comandos, configuración, UTF-8) y el Engine con PDFium
  real (abrir, renderizar, buscar, insertar, reemplazar, anotar, páginas, guardar).
- `pdflite_uitests`: **la app completa** en una ventana oculta, manejada con eventos sintéticos de
  SDL (ratón, rueda, teclado, texto, soltar archivos). Cubre los flujos de usuario de punta a
  punta y guarda capturas en `build/<preset>/tests/test_output/ui/`. Ver
  [ADR 0006](docs/adr/0006-pruebas-de-interfaz.md).

## Documentación

- [Arquitectura](docs/ARCHITECTURE.md) y [decisiones (ADR)](docs/adr/)
- [Dependencias y licencias](docs/DEPENDENCIES.md)
- [Hoja de ruta](docs/ROADMAP.md) · [Fase 7 (propuesta)](docs/FASE7.md) ·
  [Métricas de ligereza](docs/BENCHMARKS.md)
- [Cómo contribuir y convenciones](CONTRIBUTING.md) · [Cambios](CHANGELOG.md)

## Licencia

[MIT](LICENSE). Las dependencias tienen sus propias licencias (todas permisivas), listadas en
[docs/DEPENDENCIES.md](docs/DEPENDENCIES.md) y copiadas en `licenses/` al instalar.
