#include <TTyUtils/TTyUtils-SSH.hpp>
#include <cassert>
int main() { ttyutils::ssh::KnownHosts hosts; hosts.add("example"); assert(hosts.contains("example")); return 0; }
