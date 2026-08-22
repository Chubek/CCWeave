#include <TTyTk/TTyTk-ColorTools.hpp>

#include <cassert>
#include <cstdint>

int main() {
    const ttytk::color::Rgb orange{0xFF, 0x87, 0x00};
    assert(ttytk::color::parse("#ff8700") == orange);
    assert(ttytk::color::parse("CYAN") == ttytk::color::Rgb{0, 255, 255});
    assert(!ttytk::color::parse("not-a-colour"));
    assert(ttytk::color::to_256({0, 0, 0}) == 0);
    assert(ttytk::color::to_16({255, 255, 255}) == 15);
    assert(ttytk::color::distance(orange, orange) == 0.0);
    assert(ttytk::color::luminance({255, 255, 255}) > 0.99);
}
