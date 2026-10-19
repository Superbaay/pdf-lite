#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <unordered_map>
#include <utility>

namespace core {

// Caché LRU (Least Recently Used) con límite por "coste" (p. ej. bytes) en vez
// de por número de elementos: una página a 400 % pesa mucho más que una
// miniatura, así que contar elementos no limitaría la memoria real.
//
// Estructura clásica: una lista doblemente enlazada mantiene el orden de uso
// (frente = más reciente) y un unordered_map apunta a los nodos para buscar en O(1).
template <typename Key, typename Value>
class LruCache {
public:
    explicit LruCache(std::size_t max_cost) : m_max_cost{max_cost} {}

    // Inserta o reemplaza. Puede expulsar elementos antiguos para respetar el límite;
    // el recién insertado nunca se expulsa (aunque por sí solo supere el límite).
    void put(const Key& key, Value value, std::size_t cost) {
        if (auto it = m_index.find(key); it != m_index.end()) {
            m_total_cost -= it->second->cost;
            m_items.erase(it->second);
            m_index.erase(it);
        }
        m_items.push_front(Entry{key, std::move(value), cost});
        m_index.emplace(key, m_items.begin());
        m_total_cost += cost;
        evict_over_limit();
    }

    // Devuelve el valor y lo marca como usado recientemente, o nullptr.
    Value* get(const Key& key) {
        auto it = m_index.find(key);
        if (it == m_index.end()) {
            return nullptr;
        }
        // splice mueve el nodo al frente sin copiar ni invalidar iteradores.
        m_items.splice(m_items.begin(), m_items, it->second);
        return &it->second->value;
    }

    // Consulta sin alterar el orden de uso.
    [[nodiscard]] const Value* peek(const Key& key) const {
        auto it = m_index.find(key);
        return it == m_index.end() ? nullptr : &it->second->value;
    }

    bool erase(const Key& key) {
        auto it = m_index.find(key);
        if (it == m_index.end()) {
            return false;
        }
        m_total_cost -= it->second->cost;
        m_items.erase(it->second);
        m_index.erase(it);
        return true;
    }

    void clear() {
        m_items.clear();
        m_index.clear();
        m_total_cost = 0;
    }

    void set_max_cost(std::size_t max_cost) {
        m_max_cost = max_cost;
        evict_over_limit();
    }

    [[nodiscard]] std::size_t size() const { return m_items.size(); }
    [[nodiscard]] std::size_t total_cost() const { return m_total_cost; }
    [[nodiscard]] std::size_t max_cost() const { return m_max_cost; }

private:
    struct Entry {
        Key key;
        Value value;
        std::size_t cost;
    };

    void evict_over_limit() {
        while (m_total_cost > m_max_cost && m_items.size() > 1) {
            const Entry& oldest = m_items.back();
            m_total_cost -= oldest.cost;
            m_index.erase(oldest.key);
            m_items.pop_back();
        }
    }

    std::list<Entry> m_items;
    std::unordered_map<Key, typename std::list<Entry>::iterator> m_index;
    std::size_t m_max_cost;
    std::size_t m_total_cost = 0;
};

} // namespace core
