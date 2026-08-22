#include <TTyTk/TTyTk-OSCTools.hpp>

#include <cassert>

int main() {
    assert(ttytk::osc::set_title("TTy") == "\x1B]2;TTy\x1B\\");
    assert(ttytk::osc::hyperlink("https://example.test", "link") ==
           "\x1B]8;;https://example.test\x1B\\link\x1B]8;;\x1B\\");
    assert(ttytk::osc::desktop_notify("done") == "\x1B]9;done\x1B\\");
    assert(ttytk::osc::palette_query(4) == "\x1B]4;4;?\x1B\\");
}
