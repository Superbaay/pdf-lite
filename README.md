# pdf-lite

Visor y editor de PDF **100 % local**, de código abierto y lo más ligero posible, escrito en
C++23 ([por qué no C++20](docs/adr/0005-cpp23.md)) sobre [PDFium](https://pdfium.googlesource.com/pdfium/), [SDL3](https://libsdl.org) y
[Dear ImGui](https://github.com/ocornut/imgui).

- Sin nube, sin telemetría, sin actualizaciones remotas: funciona sin internet.
- CPU ~0 % en reposo: la interfaz solo se redibuja cuando hay eventos.
- Arquitectura modular: PDFium aislado en `src/pdf/`, funciones nuevas como módulos.

> Estado: **Fase 0 (scaffolding)**. Ver [docs/ROADMAP.md](docs/ROADMAP.md).

## Requisitos (Windows 10/11 x64)

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
3. Pulsa **F5**: compila y lanza `pdf_lite.exe` con el depurador.
4. Pruebas: paleta de comandos → *CMake: Run Tests*, o el panel *Testing*.

La primera configuración tarda varios minutos porque vcpkg compila SDL3, ImGui, etc.
Las siguientes usan la caché en `.vcpkg-cache/` (dentro del repo).

### Desde la terminal ("Developer PowerShell for VS 2022")

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release
ctest --preset msvc-release
./build/msvc-release/src/pdf_lite.exe
```

Presets disponibles: `msvc-debug` (con AddressSanitizer), `msvc-release`, `clang-debug`,
`clang-release`. La carpeta distribuible es `build/<preset>/src/` (exe + `pdfium.dll` +
`assets/`), o `cmake --install build/msvc-release` → `out/msvc-release/`.

## Limitación importante: editar un PDF no es como editar un .docx

Un PDF no guarda párrafos: guarda **objetos de texto con glifos en posiciones fijas**. No hay
reflujo: si un texto nuevo es más largo, no empuja al resto. Por eso la edición va por niveles:

| Nivel | Qué hace | Estado |
|---|---|---|
| 1 — Inserción | Crear texto (fuente estándar o TTF incrustada) e imágenes nuevas; mover, redimensionar, cambiar fuente/tamaño/color. | Fase 3 |
| 2 — Reemplazo | Seleccionar un objeto de texto existente, quitarlo del contenido, insertar el nuevo en su posición y guardar como **copia completa** (no incremental) para que el texto original no quede escondido en el archivo. El reemplazo es **por objeto/línea, sin reflujo**. | Fase 4 |
| 3 — Redacción, formularios, firma | Solo investigación y plan. | Fase 7 |

## Documentación

- [Arquitectura](docs/ARCHITECTURE.md) y [decisiones (ADR)](docs/adr/)
- [Dependencias y licencias](docs/DEPENDENCIES.md)
- [Hoja de ruta](docs/ROADMAP.md) · [Métricas de ligereza](docs/BENCHMARKS.md)
- [Cómo contribuir y convenciones](CONTRIBUTING.md) · [Cambios](CHANGELOG.md)

## Licencia

[MIT](LICENSE). Las dependencias tienen sus propias licencias (todas permisivas), listadas en
[docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).
