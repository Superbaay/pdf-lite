# Métricas de ligereza

Medidas reales, nunca estimadas. Equipo: AMD Ryzen 7 5825U (8 núcleos / 16 hilos), 32 GB,
Windows 10 Pro 64 bits. Build `msvc-release`.

Cómo se mide:

- **Tamaño**: suma de `out/msvc-release/` tras `cmake --install build/msvc-release`.
- **Arranque**: desde `Start-Process` hasta que la ventana tiene handle (`MainWindowHandle`).
- **RAM**: *working set* y memoria privada del proceso 3 s después de abrir.
- **CPU en reposo**: tiempo de CPU consumido por el proceso durante 10 s sin interacción.

## Fase 0 — 2026-10-18 (ventana vacía, sin PDF)

| Métrica | Valor |
|---|---|
| Distribuible total | **11,39 MB** (`pdfium.dll` 7 208 KB, `pdf_lite.exe` 2 747 KB, fuentes 1 701 KB) |
| Arranque en frío hasta ventana | **248 ms** |
| RAM (working set / privada) | **47,6 MB / 34,0 MB** |
| CPU en reposo (10 s) | **0 ms** (0 %) |
| RAM con PDF de ~10 / 300+ páginas | n/a (el visor llega en la Fase 1) |

Notas:

- La mayor parte del working set son el driver de D3D11 y las DLL del sistema que carga
  SDL_Renderer; se estudiará en la Fase 1.
- `pdfium.dll` es el 63 % del tamaño. Es la build oficial sin V8 ni XFA, la más pequeña
  disponible precompilada.
