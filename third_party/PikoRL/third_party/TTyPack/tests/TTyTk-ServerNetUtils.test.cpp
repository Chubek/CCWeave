#include <TTyTk/TTyTk-ServerNetUtils.hpp>
#include <cassert>
int main() { ttytk::net::TlsServer server{0}; assert(!server.available()); }
