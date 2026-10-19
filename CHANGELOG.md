# Changelog

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y
[versionado semántico](https://semver.org/lang/es/).

## [0.6.0] — 2026-10-18

Fases 1 a 6. Se desarrollaron en conjunto, así que se publican juntas.

### Añadido
- **Fase 1 — Visor**: abrir por diálogo nativo, arrastrar y soltar o línea de comandos;
  scroll continuo; zoom con Ctrl+rueda anclado al cursor, ajustar ancho/página y 100 %;
  miniaturas; ir a página (Ctrl+G); tema claro/oscuro; archivos recientes con última página
  y zoom; pantalla de bienvenida; ayuda de atajos (F1).
- **Fase 2 — Navegación**: búsqueda en todo el documento con resaltado y opciones
  (mayúsculas, palabra completa); índice/marcadores del PDF; selección y copia de texto.
- **Fase 3 — Edición nivel 1**: insertar texto (fuentes estándar o Inter incrustada) e
  imágenes PNG/JPG/BMP; mover, redimensionar y borrar objetos; deshacer/rehacer (patrón
  Command); guardar y guardar como (copia completa, escritura atómica).
- **Fase 4 — Edición nivel 2**: reemplazar el texto de objetos existentes conservando
  línea base y orientación; el original desaparece del archivo al guardar.
- **Fase 5 — Páginas**: rotar (deshacible), eliminar, mover, unir otro PDF al final y
  extraer un rango a un PDF nuevo.
- **Fase 6 — Anotaciones**: resaltado, subrayado, tachado, notas y dibujo libre.
- Motor asíncrono `pdf::Engine` con un hilo worker único para PDFium y renders cancelables.
- Caché LRU de texturas con límite de memoria; carga de PDFs bajo demanda (rutas Unicode).
- Diálogo de cambios sin guardar al abrir otro archivo, cerrar o salir.
- Pruebas de interfaz de punta a punta (`pdflite_uitests`) con ventana oculta.
- Licencias de terceros copiadas a `licenses/` al instalar.
- Documentación: arquitectura ampliada, ADR 0006 y 0007, plan de la Fase 7.

### Corregido
- Iconos que mostraban letras: Inter tiene glifos en la zona de uso privado de Unicode
  (se excluye esa zona de Inter y se limita Lucide a ella).
- El cambio de tema a mitad de cuadro dejaba colores del tema anterior.

### Cambiado
- Sin `-Wdouble-promotion` en clang-cl (la API de PDFium usa `double`; solo era ruido).

## [0.0.1] — 2026-10-18

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

[0.6.0]: https://github.com/Superbaay/pdf-lite/releases/tag/v0.6.0
