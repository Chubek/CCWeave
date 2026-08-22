#include <TTyTk/TTyTk-ImageProto.hpp>
#include <cassert>
#include <vector>
int main() { std::vector<ttytk::color::Rgb> pixels{{255, 0, 0}}; assert(ttytk::image::Kitty::encode(pixels, 1, 1).find("_G") != std::string::npos); }
