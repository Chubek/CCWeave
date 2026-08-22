#include <TTyUtils/TTyUtils-Button.hpp>
#include <cassert>
int main() { ttyutils::ui::Button button{"OK"}; bool hit=false; button.on_activate([&]{hit=true;}); button.activate(); assert(hit); return 0; }
