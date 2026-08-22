#include <TTyTk/TTyTk-FlowControl.hpp>

#include <cassert>

int main() {
    ttytk::flow::Throttle throttle{4};
    assert(throttle.wait_for_capacity(4));
    assert(!throttle.wait_for_capacity(1));
    throttle.acknowledge(4);
    assert(throttle.wait_for_capacity(1));

    ttytk::flow::Backpressure pressure{3};
    pressure.add(3);
    assert(pressure.active());
    pressure.consume(3);
    assert(!pressure.active());

    ttytk::flow::XonXoff xon_xoff{};
    xon_xoff.feed(0x13);
    assert(xon_xoff.paused());
    xon_xoff.feed(0x11);
    assert(!xon_xoff.paused());
}
