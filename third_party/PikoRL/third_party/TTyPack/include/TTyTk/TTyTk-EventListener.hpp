#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-KeyListener.hpp>
#include <TTyTk/TTyTk-MouseListener.hpp>

#include <functional>
#include <queue>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ttytk {

// NOTE(agent): The manifest requires unified resize and user events but does
// not expose their payload types. Resize and UserEvent are minimal variants.
struct Resize {
    unsigned rows{};
    unsigned columns{};
};

struct UserEvent {
    std::string name{};
};

using Event = std::variant<input::Key, input::Mouse, input::Paste, Resize, UserEvent>;

class EventBus {
public:
    inline void post(Event event) {
        for (const auto& callback : subscribers_) callback(event);
        events_.push(std::move(event));
    }

    inline void subscribe(std::function<void(const Event&)> callback) {
        subscribers_.push_back(std::move(callback));
    }

    inline auto poll(Event& event) -> bool {
        if (events_.empty()) return false;
        event = std::move(events_.front());
        events_.pop();
        return true;
    }

private:
    std::queue<Event> events_{};
    std::vector<std::function<void(const Event&)>> subscribers_{};
};

}  // namespace ttytk
