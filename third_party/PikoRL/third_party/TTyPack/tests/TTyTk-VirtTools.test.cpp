#include <TTyTk/TTyTk-VirtTools.hpp>
#include <cassert>
int main() { ttytk::VirtualTerm terminal{1, 2}; terminal.input("A"); assert(terminal.screen().at(0, 0).grapheme == "A"); }
