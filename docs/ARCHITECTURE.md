# Arquitectura

## Capas

```mermaid
flowchart TD
    app["app/ — main, ventana SDL3, bucle de eventos"]
    modules["modules/* — viewer, navigation, search, text_edit, images, pages, export"]
    ui["ui/ — tema, fuentes, widgets"]
    core["core/ — Document, comandos (undo/redo), bus de eventos, registro de módulos, caché LRU"]
    pdf["pdf/ — RAII sobre PDFium + hilo PDF worker"]
    pdfium[("pdfium.dll")]
    sdl[("SDL3")]
    imgui[("Dear ImGui")]

    app --> modules
    app --> ui
    modules --> core
    modules --> ui
    core --> pdf
    pdf --> pdfium
    app --> sdl
    ui --> imgui
```

Reglas:

- **Solo `pdf/` conoce PDFium.** CMake lo garantiza: `pdfium` se enlaza como `PRIVATE` en
  `pdflite_pdf`, así que ningún otro target tiene sus headers en el include path.
- **Los módulos no se conocen entre sí.** Se comunican mediante `core` (bus de eventos,
  comandos, documento compartido).
- **`core` no depende de ImGui ni de SDL**, para poder probarlo con doctest sin ventana.

## Hilos

PDFium no es thread-safe. Todas sus llamadas ocurren en un único hilo, el *PDF worker*:

```mermaid
sequenceDiagram
    participant UI as Hilo UI (SDL + ImGui)
    participant Q1 as Cola de peticiones
    participant W as PDF worker
    participant Q2 as Cola de resultados

    UI->>Q1: render(página 3, zoom 1,5)
    Note over UI: sigue dibujando, nunca se bloquea
    Q1->>W: tarea
    W->>W: FPDF_RenderPageBitmap
    W->>Q2: bitmap listo
    W-->>UI: SDL_PushEvent (despierta el bucle)
    UI->>Q2: recoge el bitmap → textura SDL
```

`SDL_PushEvent` es thread-safe y despierta a `SDL_WaitEvent`, así que la UI duerme mientras
no haya ni interacción ni resultados pendientes.

## Bucle principal

```mermaid
stateDiagram-v2
    [*] --> Durmiendo
    Durmiendo --> Dibujando: evento (ratón, teclado, resultado del worker)
    Dibujando --> Dibujando: quedan cuadros pendientes
    Dibujando --> Durmiendo: 0 cuadros pendientes → SDL_WaitEvent
```

Tras cada evento se dibujan unos pocos cuadros (`frames_after_event`) para que ImGui asiente
hover y animaciones. Con la ventana minimizada no se dibuja nada.

## Módulos (Fase 1+)

```cpp
class Module {
public:
    virtual ~Module() = default;
    virtual std::string_view id() const = 0;
    virtual void init(AppContext&) = 0;
    virtual void draw_ui() = 0;
};
```

Los módulos se registran en tiempo de compilación en una lista en `app/`. `AppContext` da
acceso a `core` (documento, comandos, eventos) pero nunca a otros módulos.

### Ruta futura: plugins como DLL

Una interfaz C estable (`extern "C"`, structs POD, tabla de punteros a función con versión)
permitiría cargar plugins con `SDL_LoadObject`. Se pospone hasta que la interfaz de módulos
se estabilice: una ABI C++ entre DLL compiladas con distintos compiladores no es estable.

## Decisiones

Ver [docs/adr/](adr/).
