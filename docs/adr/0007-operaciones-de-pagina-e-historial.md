# ADR 0007 — Operaciones de página y el historial de deshacer

- Estado: aceptado
- Fecha: 2026-10-18

## Contexto

Los comandos de edición identifican objetos con `ObjectId` (página + handle de PDFium).
Eliminar, mover o insertar páginas cambia la numeración y obliga a descargar las páginas
cargadas, así que esos `ObjectId` dejan de existir.

## Decisión

- **Rotar** es un comando deshacible (rotar en sentido contrario).
- **Eliminar, mover y unir** vacían el historial. La app pide confirmación cuando hay
  historial que se perdería, y marca el documento como modificado (`structure_dirty`).
- **Guardar sobre el archivo abierto** también vacía el historial (el documento se reabre).

## Alternativa descartada (por ahora)

Instantáneas: serializar el documento a memoria antes de cada operación estructural y
restaurarlo al deshacer. Es genérico pero cuesta RAM (un PDF de 50 MB por cada paso) y
también invalida los `ObjectId`. Queda en la hoja de ruta.
