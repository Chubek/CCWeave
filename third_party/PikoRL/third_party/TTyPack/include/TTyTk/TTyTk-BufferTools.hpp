#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-CellBuffer.hpp>
#include <TTyTk/TTyTk-EscapeCodes.hpp>

#include <string>

namespace ttytk::bufutil {
[[nodiscard]] inline auto dump_text(const CellBuffer& buffer) -> std::string {
    std::string out{}; for(std::size_t row=0;row<buffer.rows();++row) { for(std::size_t col=0;col<buffer.columns();++col) if(!buffer.at(row,col).continuation) out+=buffer.at(row,col).grapheme; if(row+1<buffer.rows())out+='\n'; } return out;
}
[[nodiscard]] inline auto dump_ansi(const CellBuffer& buffer) -> std::string { return dump_text(buffer); }
[[nodiscard]] inline auto compare(const CellBuffer& left,const CellBuffer& right) -> bool { return dump_text(left)==dump_text(right); }
[[nodiscard]] inline auto diff(const CellBuffer& left,const CellBuffer& right) -> std::string { return compare(left,right)?std::string{}:dump_text(right); }
}  // namespace ttytk::bufutil
