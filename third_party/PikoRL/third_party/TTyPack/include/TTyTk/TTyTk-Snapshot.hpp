#pragma once
#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-CellBuffer.hpp>
#include <TTyTk/TTyTk-BufferTools.hpp>
#include <string>
namespace ttytk::snapshot {
using State = CellBuffer;
[[nodiscard]] inline auto capture(const CellBuffer& buffer)->State { return buffer; }
[[nodiscard]] inline auto diff(const State& lhs,const State& rhs)->std::string { if(lhs.rows()!=rhs.rows()||lhs.columns()!=rhs.columns()) return "size differs"; for(std::size_t r=0;r<lhs.rows();++r) for(std::size_t c=0;c<lhs.columns();++c) if(lhs.at(r,c).grapheme!=rhs.at(r,c).grapheme) return "cell differs"; return {}; }
[[nodiscard]] inline auto serialize(const State& state)->std::string { std::string out; for(std::size_t r=0;r<state.rows();++r){for(std::size_t c=0;c<state.columns();++c) out+=state.at(r,c).grapheme; out+='\n';} return out; }
} // namespace ttytk::snapshot
