#include <TTyTk/TTyTk-PromptTools.hpp>
#include <cassert>
int main() { ttytk::prompt::SegmentList list{{"x"}}; assert(ttytk::prompt::render(list).find('x') != std::string::npos); }
