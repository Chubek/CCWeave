#include <TTyTk/TTyTk-Grampheme.hpp>

#include <cassert>

int main() {
    assert(ttytk::grapheme::count("a\xCC\x81" "b") == 2);
    assert(ttytk::grapheme::count("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8") == 1);
    assert(ttytk::grapheme::count("\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB") == 1);
    const auto segments = ttytk::grapheme::segment("a\xCC\x81" "b");
    assert(segments.size() == 2);
    assert(segments[0] == "a\xCC\x81");
    assert(ttytk::grapheme::next_break("ab") == 1);
}
