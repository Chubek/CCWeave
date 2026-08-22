#include <TTyUtils/TTyUtils-KeyMap.hpp>
#include <cassert>
int main() { ttyutils::keymap::Map map; map.bind("C-x C-s","save"); assert(map.lookup("C-x C-s")=="save"); return 0; }
