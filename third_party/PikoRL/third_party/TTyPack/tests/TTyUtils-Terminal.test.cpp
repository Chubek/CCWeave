#include <TTyUtils/TTyUtils-Terminal.hpp>
#include <cassert>

int main() {
    ttyutils::term::Emulator emulator{2, 5};
    std::string osc_code;
    emulator.on_osc([&](std::string code, std::string) { osc_code = std::move(code); });
    emulator.feed("hello\r\nworld\x1b]2;title\a");
    assert(emulator.screen()[0] == "hello");
    assert(emulator.screen()[1] == "world");
    assert(osc_code == "2");
    return 0;
}
