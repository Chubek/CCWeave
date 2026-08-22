#include <TTyTk/TTyTk-NotifyTools.hpp>
#include <cassert>
int main() { assert(ttytk::notify::show("title", "body").find("777") != std::string::npos); assert(ttytk::notify::progress(1, 2).find("50") != std::string::npos); }
