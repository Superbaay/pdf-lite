#pragma once

#include "core/lru_cache.hpp"
#include "modules/context.hpp"
#include "ui/texture.hpp"

#include <cstdint>
#include <unordered_map>

namespace modules {

// Caché LRU de páginas renderizadas como texturas de GPU, con límite de memoria.
// La comparten el viewer y las miniaturas (cada uno con su instancia y canal).
//
// get() nunca bloquea: devuelve la mejor textura disponible (aunque sea de otro
// zoom o de una revisión anterior: se ve borrosa un instante) y, si hace falta,
// pide al Engine un render nuevo.
class PageTextureCache {
public:
    PageTextureCache(AppContext& ctx, pdf::RenderChannel channel, std::size_t max_bytes);

    const ui::Texture* get(int page, float scale);
    // Olvida lo pendiente (p. ej. al cambiar de zoom) sin borrar texturas.
    void cancel_pending();
    // Borra todo (nuevo documento o cambio de estructura).
    void clear();
    [[nodiscard]] std::size_t bytes() const { return m_cache.total_cost(); }

private:
    struct Entry {
        ui::Texture texture;
        float scale = 0.0f;
        std::uint64_t revision = 0;
    };
    struct Pending {
        std::uint64_t ticket = 0;
        float scale = 0.0f;
        std::uint64_t revision = 0;
    };

    AppContext& m_ctx;
    pdf::RenderChannel m_channel;
    core::LruCache<int, Entry> m_cache;
    std::unordered_map<int, Pending> m_pending;
    std::uint64_t m_next_ticket = 1;
    std::uint64_t m_generation = 0; // doc.generation al que pertenece el contenido
};

} // namespace modules
