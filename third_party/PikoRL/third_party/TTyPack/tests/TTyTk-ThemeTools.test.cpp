#include <TTyTk/TTyTk-ThemeTools.hpp>
#include <cassert>
int main(){auto t=ttytk::theme::builtin("solarized-dark");assert(t.name=="solarized-dark");assert(ttytk::theme::load("x"));}
