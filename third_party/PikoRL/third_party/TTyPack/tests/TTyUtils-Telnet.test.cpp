#include <TTyUtils/TTyUtils-Telnet.hpp>
#include <cassert>
int main() { assert(ttyutils::telnet::Negotiator::escape("a\xff").size()==3); return 0; }
