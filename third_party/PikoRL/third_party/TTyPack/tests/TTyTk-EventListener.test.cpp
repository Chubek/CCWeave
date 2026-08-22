#include <TTyTk/TTyTk-EventListener.hpp>

#include <cassert>

int main() {
    ttytk::EventBus bus;
    bool notified = false;
    bus.subscribe([&](const ttytk::Event&) { notified = true; });
    bus.post(ttytk::Resize{24, 80});
    ttytk::Event event;
    assert(bus.poll(event));
    assert(notified);
    const auto* resize = std::get_if<ttytk::Resize>(&event);
    assert(resize != nullptr && resize->columns == 80);
    assert(!bus.poll(event));
}
