# Métricas de ligereza

Medidas reales, nunca estimadas. Equipo: AMD Ryzen 7 5825U (8 núcleos / 16 hilos), 32 GB,
Windows 10 Pro 64 bits, pantalla al 100 %. Build `msvc-release`.

Cómo se mide:

- **Tamaño**: suma de `out/msvc-release/` tras `cmake --install build/msvc-release`.
- **Arranque**: desde `Start-Process` hasta que la ventana tiene handle (`MainWindowHandle`).
- **RAM**: *working set* y memoria privada del proceso 4 s después de abrir.
- **CPU en reposo**: tiempo de CPU consumido por el proceso durante 10 s sin interacción.
- PDFs de prueba: texto Helvetica en cada página, carta (612×792 pt), con índice.

## v0.6.0 — 2026-10-18 (fases 1–6)

| Métrica | Valor |
|---|---|
| Distribuible total | **12,44 MB** (`pdfium.dll` 7 208 KB, `pdf_lite.exe` 3 370 KB, fuentes 1 701 KB, licencias) |
| Arranque en frío hasta ventana (sin documento) | **156 ms** |
| Arranque abriendo PDF de 10 páginas | **96 ms** |
| Arranque abriendo PDF de 320 páginas | **110 ms** |
| RAM sin documento (working set / privada) | 39,4 MB / 30,9 MB |
| RAM con PDF de 10 páginas | **42,8 MB / 47,6 MB** |
| RAM con PDF de 320 páginas | **43,2 MB / 47,5 MB** |
| CPU en reposo (10 s) — sin documento, 10 y 320 páginas | **0,0 ms** en los tres casos |

Notas:

- La RAM casi no crece con el número de páginas: solo se cargan y renderizan las visibles;
  el PDF se lee del disco bajo demanda.
- El exe pasó de 2,7 a 3,4 MB al añadir los 8 módulos, stb_image y nlohmann/json.
- El arranque con documento es menor que sin documento por la caché de disco del sistema
  (la medición sin documento fue la primera de la serie).

## Fase 0 — 2026-10-18 (ventana vacía, sin PDF)

| Métrica | Valor |
|---|---|
| Distribuible total | 11,39 MB |
| Arranque en frío hasta ventana | 248 ms |
| RAM (working set / privada) | 47,6 MB / 34,0 MB |
| CPU en reposo (10 s) | 0 ms |
