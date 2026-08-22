#include <TTyTk/TTyTk-RectOps.hpp>

#include <array>
#include <cassert>

int main() {
    struct Buffer {};
    Buffer buffer;
    const std::array region{2, 2, 10, 40};
    ttytk::rect::copy(buffer, region, region);
    ttytk::rect::fill(buffer, region, U'*', 0);
    ttytk::rect::erase(buffer, region);
    assert(ttytk::rect::checksum(buffer, region) == 0);
}
