#include <TTyUtils/TTyUtils-StatusBar.hpp>
#include <cassert>
int main() { ttyutils::ui::StatusBar bar; bar.left({{"mode"}}); bar.flash("ok"); assert(bar.message()=="ok"); return 0; }
