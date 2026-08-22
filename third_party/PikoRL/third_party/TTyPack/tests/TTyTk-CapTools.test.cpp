#include <TTyTk/TTyTk-CapTools.hpp>

#include <cassert>

int main() {
    bool called = false;
    ttytk::caps::query_async([&called](const ttytk::caps::Info& info) {
        called = info.has_truecolor;
    });
    ttytk::caps::on_response("RGB");
    assert(called);
}
