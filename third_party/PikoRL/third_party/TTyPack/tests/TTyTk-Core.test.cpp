#include <TTyTk/TTyTk-Core.hpp>

#include <cassert>

int main() {
    constexpr auto v = ttytk::version();
    static_assert(v.size() == 3);

    ttytk::Result<int> value{42};
    assert(value);
    assert(value.value() == 42);

    ttytk::Result<int> error{ttytk::Error{7, "failure"}};
    assert(!error);
    assert(error.error().code == 7);
}
