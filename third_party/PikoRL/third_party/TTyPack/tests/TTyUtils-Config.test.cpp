#include <TTyUtils/TTyUtils-Config.hpp>
#include <cassert>
int main() { ttyutils::config::Store store; store.set("x", ttyutils::config::Value{42LL}); assert(store.get<long long>("x")==42); return 0; }
