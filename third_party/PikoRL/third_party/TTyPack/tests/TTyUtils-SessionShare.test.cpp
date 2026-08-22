#include <TTyUtils/TTyUtils-SessionShare.hpp>
#include <cassert>
int main() { ttyutils::p2p::Node node{{"id"}}; ttyutils::share::Host host{node}; assert(ttyutils::share::Guest::join(host.invite(ttyutils::share::Permissions::ReadOnly)).permission()==ttyutils::share::Permissions::ReadOnly); return 0; }
