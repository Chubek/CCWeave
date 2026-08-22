#pragma once
#include <TTyTk/TTyTk-Core.hpp>
#include <cstddef>
#include <set>
namespace ttytk {
class Tabstops {
public:
 Tabstops() { for(std::size_t c=8;c<1000;c+=8) stops_.insert(c); }
 inline void set(std::size_t column) { stops_.insert(column); }
 inline void clear(std::size_t column) { stops_.erase(column); }
 inline void clear_all() noexcept { stops_.clear(); }
 [[nodiscard]] inline auto next(std::size_t column) const noexcept -> std::size_t { auto i=stops_.upper_bound(column); return i==stops_.end()?column:*i; }
 [[nodiscard]] inline auto prev(std::size_t column) const noexcept -> std::size_t { auto i=stops_.lower_bound(column); return i==stops_.begin()?column:*--i; }
private: std::set<std::size_t> stops_{};
};
} // namespace ttytk
