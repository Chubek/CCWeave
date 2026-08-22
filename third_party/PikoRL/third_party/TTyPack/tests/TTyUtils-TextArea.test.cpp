#include <TTyUtils/TTyUtils-TextArea.hpp>
#include <cassert>
int main() { ttyutils::ui::TextArea area; area.set_text("hello"); assert(area.text()=="hello"); area.options().wrap=true; assert(area.options().wrap); return 0; }
