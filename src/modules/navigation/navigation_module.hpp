#pragma once

#include "modules/context.hpp"
#include "modules/page_texture_cache.hpp"

#include <memory>
#include <string>
#include <vector>

namespace modules {

// Panel lateral (miniaturas e índice), ir a página y página anterior/siguiente.
class NavigationModule final : public Module {
public:
    [[nodiscard]] std::string_view id() const override { return "navigation"; }
    void init(AppContext& ctx) override;
    void draw_ui() override;
    void draw_toolbar() override;
    void handle_shortcuts() override;

private:
    void draw_thumbnails();
    void draw_outline_items(const std::vector<pdf::OutlineItem>& items);
    void load_outline();

    AppContext* m_ctx = nullptr;
    std::unique_ptr<PageTextureCache> m_thumbs;
    std::vector<pdf::OutlineItem> m_outline;
    bool m_outline_loaded = false;
    int m_last_current = -1; // para auto-desplazar la lista de miniaturas
    char m_page_input[16] = {};
    bool m_editing_page = false;
    bool m_focus_page_input = false;
};

} // namespace modules
