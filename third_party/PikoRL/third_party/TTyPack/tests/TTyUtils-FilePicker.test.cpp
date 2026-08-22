#include <TTyUtils/TTyUtils-FilePicker.hpp>
#include <cassert>
int main() { ttyutils::ui::FilePicker picker; picker.mode(ttyutils::ui::PickMode::Directory); assert(picker.show().has_value()); return 0; }
