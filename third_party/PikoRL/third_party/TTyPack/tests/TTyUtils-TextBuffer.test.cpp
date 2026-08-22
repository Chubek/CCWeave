#include <TTyUtils/TTyUtils-TextBuffer.hpp>
#include <cassert>

int main() {
    ttyutils::textbuf::Buffer buffer{"one\né\n"};
    assert(buffer.line_index().lines() == 3);
    buffer.insert({1, 1}, "x");
    assert(buffer.line(1) == "éx");
    auto snapshot = buffer.snapshot();
    buffer.insert({0, 0}, "!");
    assert(snapshot.text() == "one\néx\n");
    assert(buffer.text().starts_with('!'));
    return 0;
}
