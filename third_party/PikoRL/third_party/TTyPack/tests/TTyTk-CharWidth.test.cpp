#include <TTyTk/TTyTk-CharWidth.hpp>

#include <cassert>

int main() {
    assert(ttytk::charwidth::of(U'A') == 1);
    assert(ttytk::charwidth::of(U'\u0301') == 0);
    assert(ttytk::charwidth::of(U'\u5B57') == 2);
    assert(ttytk::charwidth::is_wide(U'\U0001F600'));
    assert(ttytk::charwidth::is_zero(U'\u200D'));
    assert(ttytk::charwidth::of_string("A\xCC\x81\xE5\xAD\x97") == 3);
    assert(ttytk::charwidth::of(U'\u00B7', true) == 2);
}
