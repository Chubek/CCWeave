#include <TTyTk/TTyTk-Renderer.hpp>
#include <cassert>
#include <sstream>
int main(){ttytk::CellBuffer b{1,1};ttytk::Renderer r;std::ostringstream o;r.render(b,o);assert(!o.str().empty());}
