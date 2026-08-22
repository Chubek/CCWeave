#include <TTyTk/TTyTk-TextLayout.hpp>
#include <cassert>
int main(){assert(ttytk::layout::measure("abc")==3);assert(ttytk::layout::wrap("abcd",2).size()==2);}
