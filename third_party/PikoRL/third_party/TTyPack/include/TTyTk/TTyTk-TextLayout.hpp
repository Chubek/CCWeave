#pragma once
#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-CharWidth.hpp>
#include <TTyTk/TTyTk-Grampheme.hpp>
#include <string>
#include <string_view>
#include <vector>
namespace ttytk::layout {
[[nodiscard]] inline auto measure(std::string_view text)->int{return charwidth::of_string(text);}
[[nodiscard]] inline auto wrap(std::string_view text,std::size_t columns)->std::vector<std::string>{std::vector<std::string> out;std::string line;int width=0;for(auto g:grapheme::segment(text)){int w=measure(g);if(columns&&width+w>static_cast<int>(columns)&&!line.empty()){out.push_back(std::move(line));line.clear();width=0;}line+=g;width+=w;}if(!line.empty()||text.empty())out.push_back(std::move(line));return out;}
[[nodiscard]] inline auto truncate(std::string_view text,std::size_t columns)->std::string{if(measure(text)<=static_cast<int>(columns))return std::string{text};if(columns==0)return {};if(columns==1)return "\xE2\x80\xA6";auto out=wrap(text,columns-1).front();return out+"\xE2\x80\xA6";}
[[nodiscard]] inline auto reflow(std::string_view text,std::size_t columns)->std::vector<std::string>{return wrap(text,columns);}
} // namespace ttytk::layout
