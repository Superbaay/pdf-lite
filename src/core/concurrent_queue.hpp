#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace core {

// Cola FIFO segura entre hilos (varios productores, varios consumidores).
//
// Por qué un mutex + condition_variable y no algo "lock-free": el volumen de
// mensajes es bajo (decenas por segundo) y esta versión es fácil de verificar.
template <typename T>
class ConcurrentQueue {
public:
    void push(T value) {
        {
            const std::scoped_lock lock{m_mutex};
            if (m_closed) {
                return;
            }
            m_items.push_back(std::move(value));
        }
        m_cv.notify_one();
    }

    // No bloquea: devuelve nullopt si la cola está vacía.
    std::optional<T> try_pop() {
        const std::scoped_lock lock{m_mutex};
        if (m_items.empty()) {
            return std::nullopt;
        }
        T value = std::move(m_items.front());
        m_items.pop_front();
        return value;
    }

    // Bloquea hasta que haya un elemento o la cola se cierre (devuelve nullopt).
    std::optional<T> wait_pop() {
        std::unique_lock lock{m_mutex};
        m_cv.wait(lock, [this] { return m_closed || !m_items.empty(); });
        if (m_items.empty()) {
            return std::nullopt;
        }
        T value = std::move(m_items.front());
        m_items.pop_front();
        return value;
    }

    // Despierta a todos los que esperan; los push posteriores se ignoran.
    void close() {
        {
            const std::scoped_lock lock{m_mutex};
            m_closed = true;
        }
        m_cv.notify_all();
    }

    void clear() {
        const std::scoped_lock lock{m_mutex};
        m_items.clear();
    }

    [[nodiscard]] bool empty() const {
        const std::scoped_lock lock{m_mutex};
        return m_items.empty();
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<T> m_items;
    bool m_closed = false;
};

} // namespace core
