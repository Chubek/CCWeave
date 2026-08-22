#include <TTyUtils/TTyUtils-Multiplex.hpp>
#include <cassert>
int main() { ttyutils::mux::Server server; auto& session=server.create("work"); session.window(0).split({"shell"}); assert(session.window(0).panes().size()==1); return 0; }
