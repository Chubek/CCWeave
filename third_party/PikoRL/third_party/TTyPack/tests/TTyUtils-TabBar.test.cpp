#include <TTyUtils/TTyUtils-TabBar.hpp>
#include <cassert>
int main() { ttyutils::ui::TabBar bar; bar.add({.title="main.cpp", .modified=true}); bar.select(0); assert(bar.tabs().size()==1); return 0; }
