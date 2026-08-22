#include <TTyUtils/TTyUtils-Progress.hpp>
#include <cassert>
int main() { ttyutils::ui::ProgressBar bar; bar.set_total(10); bar.advance(5); assert(bar.fraction()==0.5); ttyutils::ui::MultiProgress mp; auto task=mp.add("x",10); task.advance(2); assert(task.value()==2); return 0; }
