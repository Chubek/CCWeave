#include <TTyTk/TTyTk-EmbedTools.hpp>
#include <cassert>
int main() { assert(ttytk::embed::wrap_shell("echo x") == "exec echo x"); }
