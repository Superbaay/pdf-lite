#pragma once

#include "modules/context.hpp"

#include <string>

namespace modules {

// Búsqueda de texto en todo el documento con resaltado (el viewer dibuja los
// resultados que este módulo deja en AppContext::search).
class SearchModule final : public Module {
public:
    [[nodiscard]] std::string_view id() const override { return "search"; }
    void init(AppContext& ctx) override;
    void draw_ui() override {}
    void draw_toolbar() override;
    void handle_shortcuts() override;

private:
    void run_search();
    void go_to(int index);
    void clear();

    AppContext* m_ctx = nullptr;
    char m_query[256] = {};
    std::string m_last_query;
    bool m_match_case = false;
    bool m_whole_word = false;
    bool m_last_case = false;
    bool m_last_word = false;
    bool m_searching = false;
    bool m_focus = false;
    std::uint64_t m_token = 0;
};

} // namespace modules
