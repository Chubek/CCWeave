#include <TTyTk/TTyTk-PsuedoTerm.hpp>

#include <cassert>

int main() {
    ttytk::PTY pty{};
    assert(pty.master_fd() == -1);
    assert(!pty.resize({24, 80, 0, 0}));
}
