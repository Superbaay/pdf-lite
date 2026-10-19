# ADR 0006 — Pruebas de interfaz con ventana oculta y eventos sintéticos

- Estado: aceptado
- Fecha: 2026-10-18

## Contexto

Las pruebas unitarias no detectan fallos de interacción (foco de teclado, coordenadas,
estados de ImGui). Automatizar con clics reales (SendInput) es frágil y, peor, mueve el
ratón y escribe en las ventanas del usuario si está usando el PC.

## Decisión

- La app vive en una biblioteca (`pdflite_app`); `main.cpp` es mínimo.
- `App` expone una API de pruebas: `step()`, `settle()` (espera al worker y aplica
  resultados), `capture()` (lee el framebuffer con `SDL_RenderReadPixels`) y `context()`.
- `pdflite_uitests` crea la app con la ventana **oculta** y una configuración aislada (no
  toca `%APPDATA%`), e inyecta `SDL_Event` sintéticos (`SDL_PushEvent`): recorren el mismo
  camino que los reales (backend SDL3 de ImGui → widgets → módulos).
- Los diálogos de archivo se sustituyen por un *stub* que devuelve rutas preparadas.

## Consecuencias

- Las pruebas cubren flujos reales: abrir, navegar, zoom, buscar, seleccionar y copiar,
  insertar/mover/borrar, reemplazar y guardar, anotar, operaciones de páginas, cambios sin
  guardar, arrastrar y soltar, tema.
- Una sola `App` por proceso (ImGui y PDFium son globales): cada caso crea y destruye la suya.
- Ya encontraron fallos reales que las pruebas unitarias no veían (iconos tapados por glifos
  de Inter, tema aplicado a mitad de cuadro, Esc en la búsqueda, selección al borde del texto).
