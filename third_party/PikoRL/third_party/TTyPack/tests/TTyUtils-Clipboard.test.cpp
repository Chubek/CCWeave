#include <TTyUtils/TTyUtils-Clipboard.hpp>
#include <cassert>
int main() { assert(ttyutils::clip::copy("hello")); assert(ttyutils::clip::paste()=="hello"); return 0; }
