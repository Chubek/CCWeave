#include <TTyTk/TTyTk-JobControl.hpp>

#include <cassert>

int main() {
    const auto result = ttytk::job::resume(999999);
    assert(!result);
}
