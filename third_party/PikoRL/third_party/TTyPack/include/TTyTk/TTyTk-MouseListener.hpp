#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-InputProto.hpp>

#include <functional>

namespace ttytk {

class MouseListener {
public:
    std::function<void(const input::Mouse&)> on_move{};
    std::function<void(const input::Mouse&)> on_click{};
    std::function<void(const input::Mouse&)> on_drag{};
    std::function<bool(const input::Mouse&)> hit_test{};

    // NOTE(agent): dispatch is the minimal delivery point for decoded mouse
    // events; region representation is not declared by the manifest.
    inline void dispatch(const input::Mouse& mouse) {
        if (hit_test && !hit_test(mouse)) return;
        if (mouse.motion) {
            if (mouse.pressed) {
                if (on_drag) on_drag(mouse);
            } else if (on_move) {
                on_move(mouse);
            }
        } else if (mouse.pressed && on_click) {
            on_click(mouse);
        }
    }
};

}  // namespace ttytk
