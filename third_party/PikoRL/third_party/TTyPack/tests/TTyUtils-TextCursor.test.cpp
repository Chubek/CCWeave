#include <TTyUtils/TTyUtils-TextCursor.hpp>
#include <cassert>

int main() {
    ttyutils::textbuf::Buffer buffer{"hello world"};
    ttyutils::textcur::CursorSet cursors{buffer};
    cursors.add({0, 0});
    cursors.move(ttyutils::textcur::Motion::word_right);
    assert(cursors.cursors().front().position().col == 5);
    cursors.insert_each("!");
    assert(buffer.text() == "hello! world");
    return 0;
}
