#include <TTyUtils/TTyUtils-Widget.hpp>
#include <cassert>

struct TestWidget final : ttyutils::ui::Widget {
    void paint(ttyutils::ui::Canvas& canvas) override { canvas.text(1, 1, "ok"); }
};

int main() {
    TestWidget widget;
    widget.set_rect({0, 0, 4, 4});
    ttyutils::ui::Canvas canvas(widget.rect());
    widget.render(canvas);
    assert(canvas.texts().size() == 1);
    assert(widget.hit_test(2, 2).widget == &widget);
    return 0;
}
