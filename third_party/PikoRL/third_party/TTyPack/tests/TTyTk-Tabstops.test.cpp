#include <TTyTk/TTyTk-Tabstops.hpp>
#include <cassert>
int main(){ ttytk::Tabstops t; assert(t.next(1)==8); t.set(3); assert(t.prev(4)==3); t.clear_all(); assert(t.next(4)==4); }
