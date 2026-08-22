#include <TTyUtils/TTyUtils-Menu.hpp>
#include <cassert>
int main() { bool called=false; ttyutils::ui::Menu menu{"File", {{"Open", {}, [&]{called=true;}}}}; menu.activate(0); assert(called); return 0; }
