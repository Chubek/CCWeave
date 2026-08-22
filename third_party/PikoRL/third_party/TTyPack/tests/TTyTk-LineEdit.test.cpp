#include <TTyTk/TTyTk-LineEdit.hpp>
#include <cassert>
int main() { ttytk::Editor editor; editor.insert(U'A'); editor.backspace(); assert(editor.line().empty()); }
