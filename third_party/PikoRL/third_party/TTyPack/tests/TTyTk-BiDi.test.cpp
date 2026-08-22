#include <TTyTk/TTyTk-BiDi.hpp>
#include <cassert>
int main(){assert(ttytk::bidi::base_direction("abc")==0);assert(ttytk::bidi::base_direction("\xD7\x90")==1);}
