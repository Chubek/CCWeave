#include <TTyUtils/TTyUtils-Notify.hpp>
#include <cassert>
int main() { assert(ttyutils::notify::send({"title","body",ttyutils::notify::Urgency::Normal})); return 0; }
