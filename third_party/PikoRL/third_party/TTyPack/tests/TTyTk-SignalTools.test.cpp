#include <TTyTk/TTyTk-SignalTools.hpp>

#include <cassert>

int main() {
    const auto result = ttytk::signals::on_sigwinch([] {});
    assert(result);
}
