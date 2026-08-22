#include <TTyTk/TTyTk-SerialPort.hpp>

#include <cassert>

int main() {
    const auto serial = ttytk::Serial::open("/no/such/serial/device", 9600);
    assert(!serial);
}
