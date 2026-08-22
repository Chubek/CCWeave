#include <TTyUtils/TTyUtils-TreeView.hpp>
#include <cassert>
int main() { ttyutils::ui::TreeView view; view.expand("/src"); assert(view.expanded("/src")); view.collapse("/src"); assert(!view.expanded("/src")); return 0; }
