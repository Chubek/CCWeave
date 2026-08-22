#include <TTyTk/TTyTk-P2PNetUtils.hpp>
#include <cassert>
int main() { ttytk::p2p::Discovery discovery; discovery.add("peer"); assert(discovery.peers().size() == 1); }
