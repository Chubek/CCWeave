#include <TTyUtils/TTyUtils-Focus.hpp>
#include <cassert>

int main() {
    ttyutils::ui::Widget root;
    root.set_rect({0, 0, 10, 2});
    bool focused = false;
    root.on_focus([&](bool value) { focused = value; });
    ttyutils::ui::FocusChain chain{root};
    assert(chain.next() == &root);
    assert(focused);
    ttyutils::ui::FocusScope scope(root);
    assert(ttyutils::ui::FocusScope::active() == &root);
    return 0;
}
