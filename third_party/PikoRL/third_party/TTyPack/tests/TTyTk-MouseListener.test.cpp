#include <TTyTk/TTyTk-MouseListener.hpp>

#include <cassert>

int main() {
    ttytk::MouseListener listener;
    unsigned clicks = 0;
    listener.hit_test = [](const ttytk::input::Mouse& mouse) { return mouse.x == 4; };
    listener.on_click = [&](const ttytk::input::Mouse&) { ++clicks; };
    listener.dispatch({0, 0, 4, 2, true, false});
    listener.dispatch({0, 0, 5, 2, true, false});
    assert(clicks == 1);
}
