# ADR 0004 — Bucle principal dirigido por eventos

- Estado: aceptado
- Fecha: 2026-10-18

## Contexto

Los ejemplos de ImGui redibujan a 60 FPS de forma continua: ~1–5 % de CPU y GPU despierta
aunque nadie toque la app. El objetivo es CPU ~0 % en reposo.

## Decisión

El hilo de UI se bloquea en `SDL_WaitEvent`. Al llegar un evento se dibujan
`frames_after_event` (3) cuadros y se vuelve a dormir. El PDF worker despierta la UI con
`SDL_PushEvent` cuando termina una tarea. Con la ventana minimizada no se dibuja.

## Consecuencias

- Las animaciones continuas (p. ej. el cursor parpadeante de un campo de texto) necesitan
  pedir cuadros explícitamente con un timeout; se resolverá cuando haya campos de texto.
- Medido en docs/BENCHMARKS.md.
