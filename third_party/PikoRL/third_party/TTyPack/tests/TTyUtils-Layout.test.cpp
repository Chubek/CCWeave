#include <TTyUtils/TTyUtils-Layout.hpp>
#include <cassert>
#include <memory>

int main() {
    ttyutils::ui::HBox box;
    box.add_child(std::make_unique<ttyutils::ui::Spacer>());
    box.add_child(std::make_unique<ttyutils::ui::Spacer>());
    box.set_rect({0, 0, 5, 1});
    box.layout();
    assert(box.children()[0]->rect().width == 3);
    assert(box.children()[1]->rect().width == 2);
    return 0;
}
