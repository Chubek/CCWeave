#include <TTyUtils/TTyUtils-Diff.hpp>
#include <cassert>

int main() {
    ttyutils::textbuf::Buffer old_buffer{"a\nb\n"};
    ttyutils::textbuf::Buffer new_buffer{"a\nc\n"};
    const auto hunks = ttyutils::diff::lines(old_buffer.snapshot(), new_buffer.snapshot());
    assert(!hunks.empty());
    const auto merged = ttyutils::diff::merge3(old_buffer.snapshot(), new_buffer.snapshot(),
                                                old_buffer.snapshot());
    assert(merged.conflicts.empty());
    return 0;
}
