#include <TTyTk/TTyTk-Fuzz.hpp>
#include <cassert>
int main() { ttytk::Automaton automaton; ttytk::fuzz::stress_test(automaton, 10); assert(!ttytk::fuzz::random_sequence(5).empty()); }
