#include <TTyTk/TTyTk-EscapeCodes.hpp>

#include <cassert>

int main() {
    assert(ttytk::esc::csi == "\x1B[");
    assert(ttytk::esc::sgr(ttytk::esc::Bold) == "\x1B[1m");
    assert(ttytk::esc::cursor_to(5, 10) == "\x1B[5;10H");
    assert(ttytk::esc::clear() == "\x1B[2J");
}
