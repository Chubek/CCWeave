#include <TTyTk/TTyTk-Selection.hpp>
#include <cassert>
int main(){ttytk::CellBuffer b{1,2}; b.put(0,0,{"A"});ttytk::Selection s;s.start(0,0);s.update(0,0);assert(s.to_text(b)=="A");}
