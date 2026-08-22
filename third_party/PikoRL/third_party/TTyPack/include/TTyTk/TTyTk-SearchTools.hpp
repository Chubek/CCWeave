#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-CellBuffer.hpp>
#include <TTyTk/TTyTk-Scrollback.hpp>

#include <regex>
#include <string_view>

namespace ttytk::search {
struct Match { std::size_t row{}, column{}, length{}; };
using Regex = std::regex;
[[nodiscard]] inline auto find(const CellBuffer& buffer,const std::string_view needle) -> std::optional<Match> { const auto text=[&]{std::string s{};for(std::size_t r=0;r<buffer.rows();++r){for(std::size_t c=0;c<buffer.columns();++c)if(!buffer.at(r,c).continuation)s+=buffer.at(r,c).grapheme;s+='\n';}return s;}(); const auto p=text.find(needle);if(p==std::string::npos)return std::nullopt;return Match{p/(buffer.columns()+1),p%(buffer.columns()+1),needle.size()}; }
[[nodiscard]] inline auto find_all(const CellBuffer& buffer,const std::string_view needle) -> std::vector<Match> { std::vector<Match> out{};if(auto m=find(buffer,needle))out.push_back(*m);return out; }
}  // namespace ttytk::search
