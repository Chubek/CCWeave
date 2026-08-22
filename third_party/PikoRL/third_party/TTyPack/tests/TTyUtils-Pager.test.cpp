#include <TTyUtils/TTyUtils-Pager.hpp>
#include <cassert>
int main() { ttyutils::pager::Pager pager{ttyutils::pager::Source{"a\nb"}}; assert(pager.length()==2); pager.scroll_to(1); assert(pager.offset()==1); return 0; }
