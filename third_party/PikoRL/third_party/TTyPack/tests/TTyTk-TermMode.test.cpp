#include <TTyTk/TTyTk-TermMode.hpp>

#include <cassert>

int main() {
    ttytk::TermMode modes{};
    modes.set(ttytk::Mode::AltScreen);
    modes.save();
    modes.reset(ttytk::Mode::AltScreen);
    assert(!modes.test(ttytk::Mode::AltScreen));
    modes.restore();
    assert(modes.test(ttytk::Mode::AltScreen));
}
