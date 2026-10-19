# ADR 0001 — PDFium precompilado de bblanchon/pdfium-binaries

- Estado: aceptado
- Fecha: 2026-10-18

## Contexto

Compilar PDFium exige depot_tools, GN y una descarga de varios GB; un build tarda cerca de una
hora. El proyecto prioriza un entorno simple para un solo desarrollador.

## Decisión

Usar el `pdfium-win-x64.tgz` de [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries)
(MIT, builds automáticas y reproducibles desde el código oficial de PDFium), sin V8 ni XFA.
`cmake/Pdfium.cmake` fija el tag (`chromium/8076`) y verifica el SHA-256 antes de extraer.

## Consecuencias

- Configurar el proyecto descarga ~3,8 MB una sola vez (en `build/_deps/`).
- Actualizar PDFium = cambiar tag + hash y volver a pasar las pruebas.
- Sin JavaScript de PDF (sin V8): menos superficie de ataque y ~9 MB menos.
