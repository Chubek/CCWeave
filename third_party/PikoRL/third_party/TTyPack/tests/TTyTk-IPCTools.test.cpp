#include <TTyTk/TTyTk-IPCTools.hpp>

#include <cassert>

int main() {
    const auto channel = ttytk::ipc::connect("/no/such/ttytk.sock");
    assert(!channel);
}
