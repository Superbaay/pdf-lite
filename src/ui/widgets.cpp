#include "ui/widgets.hpp"

namespace ui {

bool icon_button(const char* id, const char* icon, const char* tooltip_text, const Theme& theme,
                 bool enabled, bool active) {
    const float side = theme.metrics().icon_button;
    ImGui::PushID(id);
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, theme.colors().accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.colors().accent);
        ImGui::PushStyleColor(ImGuiCol_Text, theme.colors().accent_text);
    }
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::Button(icon, {side, side});
    ImGui::EndDisabled();
    if (active) {
        ImGui::PopStyleColor(3);
    }
    if (tooltip_text != nullptr) {
        // AllowWhenDisabled: el tooltip explica también por qué está desactivado.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip |
                                 ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", tooltip_text);
        }
    }
    ImGui::PopID();
    return pressed && enabled;
}

void tooltip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::SetTooltip("%s", text);
    }
}

void toolbar_separator(const Theme& theme) {
    ImGui::SameLine();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = theme.metrics().icon_button;
    ImGui::GetWindowDrawList()->AddLine({pos.x, pos.y + h * 0.2f}, {pos.x, pos.y + h * 0.8f},
                                        Theme::u32(theme.colors().border), 1.0f);
    ImGui::Dummy({1.0f, h});
    ImGui::SameLine();
}

void centered_text(const char* text, bool dim) {
    const float width = ImGui::CalcTextSize(text).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > width) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - width) * 0.5f);
    }
    if (dim) {
        ImGui::TextDisabled("%s", text);
    } else {
        ImGui::TextUnformatted(text);
    }
}

} // namespace ui
