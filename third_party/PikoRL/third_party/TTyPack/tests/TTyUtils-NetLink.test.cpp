#include <TTyUtils/TTyUtils-NetLink.hpp>
#include <cassert>
int main() { const auto resolved=ttyutils::net::Resolver::resolve("localhost",80); assert(!resolved.empty()); return 0; }
