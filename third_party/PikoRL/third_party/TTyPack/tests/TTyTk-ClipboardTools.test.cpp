#include <TTyTk/TTyTk-ClipboardTools.hpp>
#include <cassert>
int main() { ttytk::clipboard::set("text"); assert(ttytk::clipboard::get() == "text"); ttytk::clipboard::clear(); assert(ttytk::clipboard::get().empty()); }
