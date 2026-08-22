#include <TTyTk/TTyTk-MuxTools.hpp>
#include <cassert>
int main() { ttytk::Mux mux; static_cast<void>(mux.add_window()); assert(mux.next_window() != nullptr); }
