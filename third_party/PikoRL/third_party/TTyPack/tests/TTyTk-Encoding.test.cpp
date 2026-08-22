#include <TTyTk/TTyTk-Encoding.hpp>

#include <array>
#include <cassert>
#include <string>

int main() {
    const std::string text = "A\xC3\xA9\xF0\x9F\x98\x80";
    const auto codepoints = ttytk::utf8::decode(text);
    assert((codepoints == std::vector<char32_t>{U'A', U'\u00E9', U'\U0001F600'}));
    assert(ttytk::utf8::encode(ttytk::Span<const char32_t>{codepoints}) == text);
    assert(ttytk::utf8::validate(text));
    assert(!ttytk::utf8::validate("\xC0\x80"));
    assert(!ttytk::utf8::validate("\xE2\x82"));
    assert((ttytk::utf8::decode("\xE2\x82") ==
            std::vector<char32_t>{U'\uFFFD', U'\uFFFD'}));

    assert(ttytk::cp::to_utf8("\x82", ttytk::cp::Codepage::cp437) == "\xC3\xA9");
    assert(ttytk::cp::from_utf8("\xC3\xA9", ttytk::cp::Codepage::cp437) == "\x82");
}
