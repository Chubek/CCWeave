#include <TTyTk/TTyTk-TermInfo.hpp>

#include <cassert>

int main() {
    std::string bytes{};
    bytes.append("\x1a\x01\x04\x00\x01\x00\x01\x00\x01\x00\x03\x00", 12);
    bytes.append("x|x\0", 4);
    bytes.push_back('\1');
    bytes.push_back('\0');  // alignment after the one-byte Boolean table
    bytes.append("\x00\x00", 2); // one numeric capability
    bytes.append("\x00\x00", 2); // offset zero for the string capability
    bytes.append("ok\0", 3);
    const auto entry = ttytk::terminfo::load_from(bytes);
    assert(entry);
    assert(entry.value().flag(0));
    assert(entry.value().num(0) == 0);
    assert(entry.value().str(0) == "ok");
    assert(ttytk::terminfo::tparm("\x1b[%i%p1%d;%p2%dH", {0, 0}) == "\x1b[1;1H");
}
