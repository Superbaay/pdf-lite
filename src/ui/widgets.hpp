#pragma once

// Widgets reutilizables con el estilo de la app.

#include "ui/theme.hpp"

#include <imgui.h>

namespace ui {

// Botón cuadrado con un icono Lucide. `active` lo pinta resaltado (herramienta
// seleccionada). Devuelve true al pulsarlo.
bool icon_button(const char* id, const char* icon, const char* tooltip, const Theme& theme,
                 bool enabled = true, bool active = false);

// Tooltip con retraso corto si el último ítem está bajo el ratón.
void tooltip(const char* text);

// Separador vertical fino para barras de herramientas.
void toolbar_separator(const Theme& theme);

// Texto centrado horizontalmente en la región disponible.
void centered_text(const char* text, bool dim = false);

} // namespace ui
