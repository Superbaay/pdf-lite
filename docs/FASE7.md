# Fase 7 — Propuesta: redacción, formularios, firma y OCR

Estado: **investigación y plan**, no implementado. Todo debe seguir siendo local y con
dependencias de licencia permisiva.

## 1. Redacción por áreas (borrado seguro)

Objetivo: el usuario dibuja un rectángulo y TODO lo que hay debajo desaparece del archivo
(no solo se tapa con un cuadro negro).

Plan:

1. Recorrer los objetos de la página (`FPDFPage_CountObjects`/`GetObject`).
2. Texto: si la caja de un objeto de texto toca el área, dividirlo por caracteres
   (`FPDFText_*` da las cajas por carácter) y recrear solo los caracteres de fuera.
3. Imágenes: si se solapan, decodificar (`FPDFImageObj_GetBitmap`), pintar el área en negro
   y volver a incrustar (`FPDFImageObj_SetBitmap`).
4. Trazados (*paths*): eliminar los que queden dentro; recortar los parciales es costoso
   → primera versión: eliminar todo el objeto si se solapa.
5. Anotaciones dentro del área: eliminar.
6. Dibujar un rectángulo negro opaco y guardar **siempre** con `FPDF_NO_INCREMENTAL`.
7. Verificación automática: extraer texto del área tras guardar y reabrir → debe estar vacío.

Riesgo: texto en *XObjects* de formulario anidados (hay que descender en
`FPDFFormObj_*`). Es la parte más delicada.

## 2. Formularios (AcroForm)

PDFium trae soporte completo vía `fpdf_formfill.h`: `FPDFDOC_InitFormFillEnvironment`,
`FORM_OnLButtonDown`, `FORM_OnChar`, `FORM_DoPageAAction`, etc. Plan:

1. Crear el entorno de formulario en la `Session` (estructura `FPDF_FORMFILLINFO` con
   callbacks; todos en el hilo worker).
2. Reenviar ratón/teclado del viewer al worker cuando el cursor está sobre un widget
   (`FPDFPage_HasFormFieldAtPoint`).
3. Renderizar con `FPDF_FFLDraw` encima de la página.
4. Sin JavaScript (build sin V8): los formularios con cálculos JS no se recalculan; avisar.

## 3. Firma

- **Firma visual** (imagen de firma manuscrita): ya es posible con el nivel 1 (insertar
  imagen). Añadir un "almacén de firmas" local en la carpeta de datos.
- **Firma digital** (PAdES): PDFium solo *lee* firmas (`fpdf_signature.h`), no las crea.
  Opciones locales:
  - Windows CNG/CryptoAPI (`CryptSignMessage`) para el CMS + escribir el diccionario `/Sig`
    con `/ByteRange` a mano. Sin dependencias nuevas, pero hay que generar el PDF
    con espacio reservado (incremental) — choca con "guardar siempre completo": la firma
    sería la ÚLTIMA operación.
  - Alternativa: biblioteca externa (p. ej. la parte de firma de *PoDoFo* es LGPL →
    **requiere aprobación**). Recomendado: CNG.

## 4. OCR local

- Motor: **Tesseract** (Apache-2.0) vía vcpkg (`tesseract`, depende de `leptonica`,
  BSD-2). Peso: ~5–8 MB + datos de idioma `spa.traineddata` (~15 MB, `tessdata_fast` ~2 MB).
- Flujo: renderizar la página a 300 ppp (worker) → Tesseract en un **segundo hilo**
  (no toca PDFium) → crear texto **invisible** (`FPDFTextObj_SetTextRenderMode` con modo
  invisible) en las posiciones devueltas → el PDF pasa a ser buscable y seleccionable.
- Debe ser opcional (módulo `ocr` que se compila con una opción de CMake) para no
  engordar el distribuible base.

## Orden propuesto

1. Firma visual + almacén (pequeño, reutiliza nivel 1).
2. Formularios (alto valor, API madura en PDFium).
3. Redacción (delicado; necesita muchas pruebas).
4. OCR opcional.
5. Firma digital con CNG.
