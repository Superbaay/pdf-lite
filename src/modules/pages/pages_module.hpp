#pragma once

#include "modules/context.hpp"

namespace modules {

// Operaciones de páginas: rotar (deshacible), eliminar, reordenar, unir
// (insertar otro PDF) y dividir (extraer páginas a un PDF nuevo).
//
// Eliminar/mover/unir cambian la numeración de páginas e invalidan los
// ObjectId de la pila de deshacer, así que vacían el historial (se avisa antes).
class PagesModule final : public Module {
public:
    [[nodiscard]] std::string_view id() const override { return "pages"; }
    void init(AppContext& ctx) override;
    void draw_ui() override;
    void draw_toolbar() override;

private:
    void on_operation(const PageOperation& op);
    void rotate(int page, int quarter_turns);
    void remove(int page);
    void move(int from, int to);
    void merge_file();
    void extract(std::vector<int> pages);
    void apply_structure(const pdf::DocInfo& info, int focus_page);

    AppContext* m_ctx = nullptr;
    bool m_range_popup = false;
    int m_range_from = 1;
    int m_range_to = 1;
};

} // namespace modules
