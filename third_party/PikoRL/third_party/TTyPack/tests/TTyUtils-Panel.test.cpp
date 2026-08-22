#include <TTyUtils/TTyUtils-Panel.hpp>
#include <cassert>
int main() { ttyutils::ui::Panel panel{"x", ttyutils::ui::Border::Rounded}; panel.set_rect({0,0,4,2}); assert(panel.rect().width == 4); return 0; }
