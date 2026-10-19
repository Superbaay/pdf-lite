# Hoja de ruta

| Fase | Contenido | Versión | Estado |
|---|---|---|---|
| 0 | Scaffolding: CMake/vcpkg, PDFium verificado, ventana SDL3+ImGui, lint, pruebas, CI, VS Code | — | ✅ |
| 1 | Visor: abrir (diálogo, arrastrar, CLI), scroll continuo, zoom, miniaturas, ir a página, tema claro/oscuro, recientes y última página | v0.1.0 | ✅ |
| 2 | Navegación: búsqueda con resaltado, índice/marcadores, selección y copia de texto | v0.2.0 | ✅ |
| 3 | Edición nivel 1 (insertar texto/imagen, mover, redimensionar, borrar), deshacer/rehacer, guardar / guardar como | v0.3.0 | ✅ |
| 4 | Edición nivel 2 (reemplazo real de objetos de texto) | v0.4.0 | ✅ |
| 5 | Páginas: rotar, eliminar, reordenar, unir y dividir | v0.5.0 | ✅ |
| 6 | Anotaciones: resaltado, subrayado, tachado, notas, dibujo libre | v0.6.0 | ✅ |
| 7 | Propuesta: redacción por áreas, formularios, firma, OCR local | — | 📋 [plan](FASE7.md) |

Las fases 1–6 se desarrollaron juntas y se publican en la versión 0.6.0 (ver CHANGELOG).

## Ideas para después

- Selección de texto entre páginas y por doble/triple clic (palabra/línea).
- Renderizado por mosaicos (*tiles*) para zoom extremo sin límite de 8192 px.
- Conservar el canal alfa de PNG (SMask) al insertar imágenes.
- Deshacer para eliminar/mover/unir páginas (instantáneas del documento en memoria).
- Impresión (`FPDF_RenderPage` sobre un HDC de impresora).
- Plugins como DLL con interfaz C (ver ARCHITECTURE.md).
