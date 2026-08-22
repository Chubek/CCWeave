#pragma once
#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-Encoding.hpp>
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace ttytk::bidi {
using Level=std::uint8_t;
[[nodiscard]] inline auto base_direction(std::string_view text)->Level{for(auto cp:utf8::decode(text)){if((cp>=0x0590&&cp<=0x08ff)||(cp>=0xfb1d&&cp<=0xfdff))return 1;if((cp>=U'A'&&cp<=U'z'))return 0;}return 0;}
[[nodiscard]] inline auto resolve(std::string_view text)->std::vector<Level>{return std::vector<Level>(utf8::decode(text).size(),base_direction(text));}
[[nodiscard]] inline auto reorder_line(std::string text,const std::vector<Level>& levels)->std::string{if(!levels.empty()&&(levels.front()&1U))std::reverse(text.begin(),text.end());return text;}
} // namespace ttytk::bidi
