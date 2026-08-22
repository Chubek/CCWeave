#include <TTyTk/TTyTk-UITools.hpp>
#include <cassert>
int main() { const ttytk::ui::Box box{ttytk::ui::DoubleBorder}; assert(!box.draw(3, 3).empty()); }
