#pragma once
#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-CellBuffer.hpp>
#include <TTyTk/TTyTk-MouseListener.hpp>
#include <algorithm>
#include <string>
#include <vector>
namespace ttytk {
struct SelectionRect { std::size_t row{}, first{}, last{}; };
class Selection {
public:
 inline void start(std::size_t row,std::size_t column){ anchor_row_=row; anchor_column_=column; row_=row; column_=column; }
 inline void update(std::size_t row,std::size_t column){ row_=row; column_=column; }
 [[nodiscard]] inline auto get_rects() const -> std::vector<SelectionRect>{ if(anchor_row_==row_) return {{row_,std::min(anchor_column_,column_),std::max(anchor_column_,column_)}}; return {}; }
 [[nodiscard]] inline auto to_text(const CellBuffer& buffer) const -> std::string { std::string out; for(auto r:get_rects()) for(std::size_t c=r.first;c<=r.last&&c<buffer.columns();++c) if(!buffer.at(r.row,c).continuation) out+=buffer.at(r.row,c).grapheme; return out; }
private: std::size_t anchor_row_{},anchor_column_{},row_{},column_{};
};
} // namespace ttytk
