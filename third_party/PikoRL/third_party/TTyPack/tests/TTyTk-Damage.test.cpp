#include <TTyTk/TTyTk-Damage.hpp>
#include <cassert>
int main(){ ttytk::Damage d; d.mark(1,4,2); d.mark(1,5,8); assert(d.regions().size()==1); d.clear(); assert(d.regions().empty()); }
