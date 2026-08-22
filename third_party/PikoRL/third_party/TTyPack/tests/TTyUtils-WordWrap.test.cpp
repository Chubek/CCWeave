#include <TTyUtils/TTyUtils-WordWrap.hpp>
#include <cassert>

int main() {
    ttyutils::textbuf::Buffer buffer{"a bb ccc"};
    auto map = ttyutils::wrap::soft_wrap(buffer.snapshot(), 4);
    assert(map.rows().size() >= 2);
    assert(map.visual_to_buffer({0, 0}).line == 0);
    return 0;
}
