#include <TTyUtils/TTyUtils-Dialog.hpp>
#include <cassert>
int main() { auto result=ttyutils::ui::confirm("x","y"); assert(result==ttyutils::ui::DialogResult::Yes); return 0; }
