#include "modules/page_texture_cache.hpp"

#include <cmath>

namespace modules {

namespace {
// Diferencia relativa de escala a partir de la cual vale la pena re-renderizar.
constexpr float rescale_threshold = 0.04f;
} // namespace

PageTextureCache::PageTextureCache(AppContext& ctx, pdf::RenderChannel channel,
                                   std::size_t max_bytes)
    : m_ctx{ctx}, m_channel{channel}, m_cache{max_bytes} {}

const ui::Texture* PageTextureCache::get(int page, float scale) {
    if (!m_ctx.doc.open || page < 0 || page >= m_ctx.doc.page_count()) {
        return nullptr;
    }
    if (m_generation != m_ctx.doc.generation) {
        clear();
        m_generation = m_ctx.doc.generation;
    }
    const std::uint64_t revision = m_ctx.doc.page_revision[static_cast<std::size_t>(page)];
    Entry* entry = m_cache.get(page);

    const bool up_to_date = entry != nullptr && entry->revision == revision &&
                            std::abs(entry->scale - scale) <= scale * rescale_threshold;
    if (!up_to_date) {
        auto it = m_pending.find(page);
        const bool already_requested =
            it != m_pending.end() && it->second.revision == revision &&
            std::abs(it->second.scale - scale) <= scale * rescale_threshold;
        if (!already_requested) {
            const std::uint64_t ticket = m_next_ticket++;
            m_pending[page] = Pending{ticket, scale, revision};
            const std::uint64_t generation = m_generation;
            m_ctx.engine.render(
                page, scale, m_channel,
                [this, page, ticket, generation, revision](pdf::Result<pdf::RenderedPage> result) {
                    auto pending = m_pending.find(page);
                    if (pending != m_pending.end() && pending->second.ticket == ticket) {
                        m_pending.erase(pending);
                    }
                    // Descartar resultados de otro documento o canceladas.
                    if (!result || generation != m_generation) {
                        return;
                    }
                    const auto& bmp = result->bitmap;
                    auto texture = ui::Texture::from_bgra(m_ctx.renderer, bmp.width, bmp.height,
                                                          bmp.pixels.data());
                    if (!texture.valid()) {
                        return;
                    }
                    // Etiquetamos con la revisión de la PETICIÓN: si la página se
                    // editó mientras tanto, la siguiente get() verá la diferencia y
                    // pedirá otro render (como mucho uno de más, nunca uno de menos).
                    const std::size_t cost = texture.bytes();
                    m_cache.put(page, Entry{std::move(texture), result->scale, revision}, cost);
                });
        }
    }
    return entry != nullptr ? &entry->texture : nullptr;
}

void PageTextureCache::cancel_pending() {
    m_ctx.engine.cancel_renders(m_channel);
    m_pending.clear();
}

void PageTextureCache::clear() {
    cancel_pending();
    m_cache.clear();
}

} // namespace modules
