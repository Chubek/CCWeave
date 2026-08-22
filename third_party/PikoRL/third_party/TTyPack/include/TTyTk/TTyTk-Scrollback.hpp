#pragma once
#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-CellBuffer.hpp>
#include <cstddef>
#include <deque>
#include <vector>
namespace ttytk {
class Scrollback {
public:
 inline void push_line(std::vector<Cell> line) { lines_.push_back(std::move(line)); while(lines_.size()>limit_) lines_.pop_front(); }
 [[nodiscard]] inline auto line(std::size_t index) const -> const std::vector<Cell>& { return lines_.at(index); }
 [[nodiscard]] inline auto size() const noexcept -> std::size_t { return lines_.size(); }
 inline void set_limit(std::size_t limit) { limit_=limit; while(lines_.size()>limit_) lines_.pop_front(); }
 inline void reflow(std::size_t columns) { for(auto& line:lines_) line.resize(columns); }
private: std::deque<std::vector<Cell>> lines_{}; std::size_t limit_{10000};
};
} // namespace ttytk
