#include <TTyUtils/TTyUtils-Theme.hpp>
#include <cassert>
#include <fstream>

int main() {
    const std::filesystem::path path{"/tmp/ttyutils-theme-test.toml"};
    std::ofstream{path} << "dark = true\nmenu.selected.fg = \"#ff0000\"\n";
    const auto theme = ttyutils::theme::load(path);
    assert(theme.dark_mode());
    assert(theme.role("menu.selected").foreground.has_value());
    return 0;
}
