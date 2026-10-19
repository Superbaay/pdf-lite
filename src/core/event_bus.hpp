#pragma once

#include <any>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core {

// Bus de eventos síncrono y tipado: los módulos publican y se suscriben a
// structs (p. ej. DocumentOpened) sin conocerse entre sí.
//
// Solo se usa desde el hilo de UI. Los eventos se encolan y se reparten en
// dispatch() (una vez por cuadro) para que un handler pueda publicar otros
// eventos sin reentrar en el bucle de reparto.
class EventBus {
public:
    template <typename Event>
    void subscribe(std::function<void(const Event&)> handler) {
        m_handlers[std::type_index{typeid(Event)}].push_back(
            [h = std::move(handler)](const std::any& e) { h(std::any_cast<const Event&>(e)); });
    }

    template <typename Event>
    void publish(Event event) {
        m_pending.emplace_back(std::type_index{typeid(Event)}, std::any{std::move(event)});
    }

    // Reparte los eventos pendientes. Devuelve true si se repartió alguno.
    bool dispatch() {
        bool any = false;
        // Los handlers pueden publicar más: repetimos hasta vaciar (con tope por seguridad).
        for (int round = 0; round < 16 && !m_pending.empty(); ++round) {
            auto batch = std::exchange(m_pending, {});
            for (const auto& [type, event] : batch) {
                any = true;
                if (auto it = m_handlers.find(type); it != m_handlers.end()) {
                    for (const auto& handler : it->second) {
                        handler(event);
                    }
                }
            }
        }
        return any;
    }

private:
    std::unordered_map<std::type_index, std::vector<std::function<void(const std::any&)>>>
        m_handlers;
    std::vector<std::pair<std::type_index, std::any>> m_pending;
};

} // namespace core
