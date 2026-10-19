#include "core/command_stack.hpp"
#include "core/concurrent_queue.hpp"
#include "core/event_bus.hpp"
#include "core/lru_cache.hpp"
#include "core/settings.hpp"
#include "core/utf.hpp"

#include <doctest/doctest.h>

#include <string>
#include <thread>
#include <vector>

TEST_CASE("LruCache expulsa por coste el elemento usado hace más tiempo") {
    core::LruCache<int, std::string> cache{100};
    cache.put(1, "a", 40);
    cache.put(2, "b", 40);
    REQUIRE(cache.get(1) != nullptr); // 1 pasa a ser el más reciente
    cache.put(3, "c", 40);            // 120 > 100: sale el 2
    CHECK(cache.peek(2) == nullptr);
    CHECK(cache.peek(1) != nullptr);
    CHECK(cache.peek(3) != nullptr);
    CHECK(cache.total_cost() == 80);

    SUBCASE("reemplazar una clave actualiza el coste") {
        cache.put(1, "a2", 10);
        CHECK(cache.total_cost() == 50);
        CHECK(*cache.get(1) == "a2");
    }
    SUBCASE("un elemento más grande que el límite se conserva solo") {
        cache.put(4, "enorme", 500);
        CHECK(cache.size() == 1);
        CHECK(cache.peek(4) != nullptr);
    }
    SUBCASE("erase y clear") {
        CHECK(cache.erase(1));
        CHECK_FALSE(cache.erase(1));
        cache.clear();
        CHECK(cache.size() == 0);
        CHECK(cache.total_cost() == 0);
    }
}

TEST_CASE("ConcurrentQueue entrega todo entre hilos y se cierra limpiamente") {
    core::ConcurrentQueue<int> queue;
    constexpr int producers = 4;
    constexpr int per_producer = 1000;
    std::vector<std::thread> threads;
    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&queue] {
            for (int i = 0; i < per_producer; ++i) {
                queue.push(1);
            }
        });
    }
    long long sum = 0;
    std::thread consumer{[&] {
        while (auto v = queue.wait_pop()) {
            sum += *v;
        }
    }};
    for (auto& t : threads) {
        t.join();
    }
    // Esperar a que el consumidor vacíe la cola antes de cerrarla.
    while (!queue.empty()) {
        std::this_thread::yield();
    }
    queue.close();
    consumer.join();
    CHECK(sum == producers * per_producer);
    queue.push(5); // tras close() se ignora
    CHECK(queue.try_pop() == std::nullopt);
}

namespace {
struct AddCommand final : core::Command {
    int& target;
    int amount;
    AddCommand(int& t, int a) : target{t}, amount{a} {}
    void execute() override { target += amount; }
    void undo() override { target -= amount; }
    [[nodiscard]] std::string description() const override { return "sumar"; }
};
} // namespace

TEST_CASE("CommandStack deshace y rehace en orden") {
    int value = 0;
    core::CommandStack stack{3};
    stack.execute(std::make_unique<AddCommand>(value, 1));
    stack.execute(std::make_unique<AddCommand>(value, 10));
    CHECK(value == 11);
    CHECK(stack.undo());
    CHECK(value == 1);
    CHECK(stack.redo());
    CHECK(value == 11);
    CHECK_FALSE(stack.redo());

    SUBCASE("una acción nueva borra el historial de rehacer") {
        stack.undo();
        stack.execute(std::make_unique<AddCommand>(value, 100));
        CHECK_FALSE(stack.can_redo());
        CHECK(value == 101);
    }
    SUBCASE("respeta la profundidad máxima") {
        for (int i = 0; i < 5; ++i) {
            stack.execute(std::make_unique<AddCommand>(value, 1));
        }
        CHECK(stack.undo_size() == 3);
    }
    SUBCASE("la revisión cambia con cada operación") {
        const auto r = stack.revision();
        stack.undo();
        CHECK(stack.revision() != r);
    }
}

TEST_CASE("EventBus reparte por tipo y permite publicar desde un handler") {
    struct Ping {
        int n;
    };
    struct Pong {
        int n;
    };
    core::EventBus bus;
    int pongs = 0;
    bus.subscribe<Ping>([&](const Ping& p) { bus.publish(Pong{p.n + 1}); });
    bus.subscribe<Pong>([&](const Pong& p) { pongs += p.n; });
    bus.publish(Ping{1});
    CHECK(bus.dispatch());
    CHECK(pongs == 2);
    CHECK_FALSE(bus.dispatch());
}

TEST_CASE("Settings: ida y vuelta por JSON con rutas Unicode") {
    core::Settings s;
    s.theme = core::ThemeMode::light;
    s.sidebar_visible = false;
    s.touch_recent(std::filesystem::path{u8"C:/Documentos/Año/canción ñ.pdf"}, 7, 1.5f);
    s.touch_recent(std::filesystem::path{"C:/b.pdf"}, 0, 0.0f);

    const auto back = core::from_json(core::to_json(s));
    CHECK(back.theme == core::ThemeMode::light);
    CHECK_FALSE(back.sidebar_visible);
    REQUIRE(back.recents.size() == 2);
    CHECK(back.recents[0].path == std::filesystem::path{"C:/b.pdf"});
    CHECK(back.recents[1].path == std::filesystem::path{u8"C:/Documentos/Año/canción ñ.pdf"});
    CHECK(back.recents[1].last_page == 7);

    SUBCASE("touch_recent mueve al frente sin duplicar (sin distinguir mayúsculas)") {
        s.touch_recent(std::filesystem::path{u8"c:/documentos/año/CANCIÓN ñ.pdf"}, 3, 1.0f);
        CHECK(s.recents.size() == 2);
    }
    SUBCASE("JSON corrupto o con tipos raros da valores por defecto") {
        const auto bad = core::from_json("{ esto no es json");
        CHECK(bad.theme == core::ThemeMode::dark);
        const auto weird = core::from_json(R"({"theme": 5, "recents": [1, {"path": 3}]})");
        CHECK(weird.recents.empty());
    }
}

TEST_CASE("UTF-8 <-> UTF-16 conserva acentos, ñ y emoji") {
    const std::string text = "¿Año? ¡Sí! ñandú 😀";
    CHECK(core::utf16_to_utf8(core::utf8_to_utf16(text)) == text);
    CHECK(core::utf8_to_utf16("ñ") == u"ñ");
    CHECK(core::utf8_to_utf16("😀").size() == 2); // par sustituto
}
