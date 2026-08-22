#include <TTyTk/TTyTk-Snapshot.hpp>
#include <cassert>
int main(){ttytk::CellBuffer b{1,1};auto s=ttytk::snapshot::capture(b);assert(ttytk::snapshot::diff(b,s).empty());}
