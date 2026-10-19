# ADR 0002 — SDL_Renderer y enlazado estático de dependencias

- Estado: aceptado
- Fecha: 2026-10-18

## Contexto

El prompt propone ImGui + SDL3 con SDL_Renderer, o Direct3D 11 si hay una mejora medible.

## Decisión

1. **SDL_Renderer**: en Windows usa Direct3D 11 por debajo, así que ya tenemos aceleración
   por GPU sin escribir código D3D11. Un visor de PDF dibuja pocas texturas grandes (páginas)
   y algo de UI: no hay una mejora medible que justifique D3D11 directo. Se revisará si
   las métricas de la Fase 1 muestran un cuello de botella.
2. **Triplet `x64-windows-static-md`**: SDL3 e ImGui se enlazan estáticamente en el exe con
   el CRT dinámico (`/MD`). El enlazador elimina el código no usado (`/OPT:REF /OPT:ICF`),
   y el distribuible queda en `pdf_lite.exe` + `pdfium.dll` + `assets/`.

## Consecuencias

- Menos DLL que distribuir y un tamaño total menor.
- Requiere el *Visual C++ Redistributable* (ya está en casi todos los Windows); se puede
  revisar con `/MT` si hiciera falta un exe totalmente autónomo.
