# Cómo contribuir

## Flujo de Git

- `main` siempre compila y pasa la CI. Nunca se hace commit directo a `main`.
- Ramas: `feat/*`, `fix/*`, `docs/*`, `refactor/*`, `chore/*`. Se fusionan por Pull Request
  con la CI en verde.
- Commits con [Conventional Commits](https://www.conventionalcommits.org/es/v1.0.0/), en
  español, pequeños y atómicos:

  ```
  feat(viewer): zoom con Ctrl+rueda

  El zoom se ancla al cursor para que el punto bajo el ratón no se mueva.
  ```

  Tipos: `feat`, `fix`, `docs`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`.
- Versiones con tags `vMAYOR.MENOR.PARCHE` y entrada en `CHANGELOG.md`.

## Convenciones de código

| Elemento | Convención | Ejemplo |
|---|---|---|
| Tipos (clases, structs, enums) | `PascalCase` | `PageCache` |
| Funciones y variables | `snake_case` | `render_page()` |
| Miembros privados/protegidos | prefijo `m_` | `m_window` |
| Constantes | `snake_case` con `constexpr` | `frames_after_event` |
| Espacios de nombres | `snake_case`, uno por carpeta | `pdf`, `core`, `ui` |

Reglas:

1. **Sin `new`/`delete` manuales.** Usa `std::make_unique`, contenedores o RAII.
2. **Sin punteros crudos con propiedad.** Un `T*` crudo solo significa "lo uso, no lo libero".
3. **Sin variables globales mutables.** Las constantes `constexpr` sí están permitidas.
4. **Errores con `std::expected<T, std::string>`** o códigos de resultado. Nunca lances
   excepciones a través de callbacks de PDFium o SDL (son APIs de C: una excepción ahí es
   comportamiento indefinido).
5. **Solo `src/pdf/` incluye headers de PDFium.** El build lo garantiza: el include path de
   PDFium es `PRIVATE` a `pdflite_pdf`.
6. **PDFium se llama desde un único hilo** (el *PDF worker*, a partir de la Fase 1).
7. Los comentarios explican el **porqué**, no el qué.

## Formato y análisis

```powershell
./scripts/format.ps1          # formatea
./scripts/format.ps1 -Check   # lo que ejecuta la CI
```

`clang-tidy` está configurado en `.clang-tidy`; en VS Code lo ejecuta la extensión C/C++
(`C_Cpp.codeAnalysis.clangTidy.enabled`).

## Añadir una dependencia

Antes de añadirla:

1. Verifica la licencia en el repositorio oficial. Permitidas: MIT, BSD, Apache-2.0, zlib,
   BSL-1.0, MPL-2.0, ISC, dominio público. GPL/AGPL/LGPL requieren aprobación explícita.
2. Verifica que esté mantenida.
3. Justifica su peso: si la STL lo resuelve en pocas líneas, no se añade.
4. Regístrala en `docs/DEPENDENCIES.md` (nombre, versión, licencia, tamaño, motivo).
