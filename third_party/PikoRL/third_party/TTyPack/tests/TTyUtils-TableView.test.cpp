#include <TTyUtils/TTyUtils-TableView.hpp>
#include <cassert>
int main() { ttyutils::ui::TableView view; view.add_column({.title="x", .width=4, .align=ttyutils::ui::Right}); assert(view.columns().size()==1); return 0; }
