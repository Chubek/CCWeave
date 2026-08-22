#include <TTyUtils/TTyUtils-LogView.hpp>
#include <cassert>
int main() { auto filter=ttyutils::logview::Filter::exclude("healthz"); assert(filter.accepts("ERROR")); assert(!filter.accepts("healthz")); return 0; }
