#include <TTyTk/TTyTk-CellBuffer.hpp>
#include <TTyTk/TTyTk-ClipboardTools.hpp>
#include <TTyTk/TTyTk-EscapeCodes.hpp>
#include <TTyTk/TTyTk-ImageProto.hpp>
#include <TTyTk/TTyTk-PsuedoTerm.hpp>
#include <cassert>

int main() {
    ttytk::Screen screen{2, 3};
    assert(screen.rows() == 2 && screen.columns() == 3);
    ttytk::Clipboard clipboard;
    clipboard.set("compat");
    assert(clipboard.get() == "compat");
    assert(ttytk::Hyperlink::open("https://example").starts_with("\x1b]8;;"));
    ttytk::Image image{1, 1, {{255, 0, 0}}};
    assert(image.pixels.size() == 1);
    ttytk::PseudoTerm terminal;
    assert(terminal.master_fd() < 0);
    return 0;
}
