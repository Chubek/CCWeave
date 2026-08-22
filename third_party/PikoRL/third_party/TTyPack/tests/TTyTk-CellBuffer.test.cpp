#include <TTyTk/TTyTk-CellBuffer.hpp>

#include <cassert>

int main() {
    ttytk::CellBuffer buffer{2, 3};
    buffer.put(0, 0, {"\xE5\xAD\x97"});
    assert(buffer.at(0, 1).continuation);
    buffer.scroll(0, 2, 1);
    assert(buffer.at(1, 0).grapheme == " ");
    buffer.resize(3, 4);
    assert(buffer.rows() == 3 && buffer.columns() == 4);
}
