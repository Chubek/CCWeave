#include <TTyUtils/TTyUtils-UndoHistory.hpp>
#include <cassert>

int main() {
    ttyutils::textbuf::Buffer buffer{"a"};
    ttyutils::undo::History history{buffer};
    { auto transaction = history.transaction(); buffer.insert({0, 1}, "b"); }
    assert(buffer.text() == "ab");
    assert(history.undo());
    assert(buffer.text() == "a");
    assert(history.redo_branch());
    assert(buffer.text() == "ab");
    return 0;
}
