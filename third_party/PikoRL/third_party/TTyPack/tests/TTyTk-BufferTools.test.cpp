#include <TTyTk/TTyTk-BufferTools.hpp>
#include <cassert>
int main() { ttytk::CellBuffer buffer{1, 2}; buffer.put(0, 0, {"A"}); assert(ttytk::bufutil::dump_text(buffer) == "A "); }
