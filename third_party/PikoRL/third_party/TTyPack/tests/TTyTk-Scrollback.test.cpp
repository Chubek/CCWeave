#include <TTyTk/TTyTk-Scrollback.hpp>
#include <cassert>
int main(){ ttytk::Scrollback s; s.set_limit(1); s.push_line({}); s.push_line({}); assert(s.size()==1); }
