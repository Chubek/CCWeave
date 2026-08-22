#include <TTyUtils/TTyUtils-Readline.hpp>
#include <cassert>

int main() {
    ttyutils::rl::Editor editor;
    editor.feed(ttytk::input::Event{ttytk::input::Key{U'h', 0, 1}});
    editor.feed(ttytk::input::Event{ttytk::input::Key{U'i', 0, 1}});
    editor.feed(ttytk::input::Event{ttytk::input::Key{U'\n', 0, 1}});
    const auto line = ttyutils::rl::read_line(editor);
    assert(line && *line == "hi");
    return 0;
}
