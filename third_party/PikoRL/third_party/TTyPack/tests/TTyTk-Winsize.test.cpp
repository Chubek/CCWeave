#include <TTyTk/TTyTk-Winsize.hpp>

#include <cassert>
#include <csignal>

int main() {
    bool changed = false;
    const auto installed = ttytk::winsize::on_change([&changed] { changed = true; });
    assert(installed);
    assert(::raise(SIGWINCH) == 0);
    const auto size = ttytk::winsize::get(-1);
    assert(!size);
    assert(changed);
}
