#include <TTyUtils/TTyUtils-Shell.hpp>
#include <cassert>
int main() { auto job=ttyutils::shell::run({"/bin/true"}); assert(job.wait()==0); return 0; }
