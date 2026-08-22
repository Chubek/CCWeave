#include <TTyUtils/TTyUtils-P2P.hpp>
#include <cassert>
int main() { ttyutils::p2p::Node node{{"id"}}; auto channel=node.dial({"peer"}); channel.send("frame"); assert(channel.frames().size()==1); return 0; }
