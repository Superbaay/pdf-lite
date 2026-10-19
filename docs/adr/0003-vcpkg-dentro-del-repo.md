# ADR 0003 — vcpkg dentro del repositorio (ignorado por git)

- Estado: aceptado
- Fecha: 2026-10-18

## Contexto

Regla del proyecto: no crear archivos fuera de la carpeta del repositorio. vcpkg suele vivir
en una ruta global y guarda su caché binaria en `%LOCALAPPDATA%`.

## Decisión

- `scripts/bootstrap.ps1` clona vcpkg en `./vcpkg/`, en el commit de `builtin-baseline`.
- `CMakePresets.json` apunta `VCPKG_DEFAULT_BINARY_CACHE` a `./.vcpkg-cache/` y
  `VCPKG_INSTALLED_DIR` a `./build/vcpkg_installed/` (compartido entre presets).
- Las tres carpetas están en `.gitignore`.

## Consecuencias

- Todo lo que genera el proyecto vive en el repo; borrar la carpeta lo limpia del todo.
- Ocupa unos GB dentro del repo (código de vcpkg + cachés).
- Las herramientas del sistema (MSVC, CMake, Ninja) sí se instalan de forma global: no hay
  alternativa razonable.
