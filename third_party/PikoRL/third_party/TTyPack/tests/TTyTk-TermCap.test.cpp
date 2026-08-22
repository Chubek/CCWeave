#include <TTyTk/TTyTk-TermCap.hpp>

#include <cassert>

int main() {
    const auto entry = ttytk::termcap::load_from("vt|test:am:co#80:cm=\\E[%i%d;%dH:");
    assert(entry);
    assert(entry.value().flag("am"));
    assert(entry.value().num("co") == 80);
    assert(entry.value().str("cm").has_value());
    assert(ttytk::termcap::tgoto(*entry.value().str("cm"), 0, 0) == "\x1b[1;1H");
}
