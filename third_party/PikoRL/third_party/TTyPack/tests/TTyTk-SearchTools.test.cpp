#include <TTyTk/TTyTk-SearchTools.hpp>
#include <cassert>
int main() { ttytk::CellBuffer buffer{1, 3}; buffer.put(0, 0, {"a"}); buffer.put(0, 1, {"b"}); assert(ttytk::search::find(buffer, "ab").has_value()); }
