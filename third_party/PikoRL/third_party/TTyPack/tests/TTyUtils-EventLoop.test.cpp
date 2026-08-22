#include <TTyUtils/TTyUtils-EventLoop.hpp>
#include <cassert>

int main() {
    ttyutils::loop::Loop loop;
    bool called = false;
    ttyutils::loop::post(loop, [&] { called = true; loop.stop(); });
    loop.run_once(std::chrono::milliseconds{0});
    assert(called);
    return 0;
}
