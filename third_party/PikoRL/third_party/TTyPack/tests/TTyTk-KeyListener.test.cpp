#include <TTyTk/TTyTk-KeyListener.hpp>

#include <cassert>

int main() {
    ttytk::KeyListener listener;
    bool callback = false;
    bool observed = false;
    listener.bind("Ctrl-C", [&] { callback = true; });
    listener.on_key = [&](const ttytk::input::Key&) { observed = true; };
    listener.dispatch({U'c', 4, 1}, "Ctrl-C");
    assert(callback && observed);
}
