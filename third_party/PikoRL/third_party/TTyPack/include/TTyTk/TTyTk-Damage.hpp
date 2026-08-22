#pragma once
#include <TTyTk/TTyTk-Core.hpp>
#include <algorithm>
#include <cstddef>
#include <vector>
namespace ttytk {
struct DamageRegion { std::size_t row{}, first{}, last{}; };
class Damage {
public:
 inline void mark(std::size_t row, std::size_t first, std::size_t last) {
   if (first > last) std::swap(first,last);
   dirty_.push_back({row,first,last});
   merge();
 }
 inline void mark_all(std::size_t rows, std::size_t columns) { dirty_.clear(); if(rows && columns) dirty_.push_back({0,0,rows*columns-1}); }
 [[nodiscard]] inline auto regions() const noexcept -> const std::vector<DamageRegion>& { return dirty_; }
 inline void clear() noexcept { dirty_.clear(); }
 inline void merge() {
   std::sort(dirty_.begin(),dirty_.end(),[](auto a,auto b){return a.row==b.row?a.first<b.first:a.row<b.row;});
   std::vector<DamageRegion> out; for(auto r:dirty_) { if(!out.empty()&&out.back().row==r.row&&r.first<=out.back().last+1) out.back().last=std::max(out.back().last,r.last); else out.push_back(r); } dirty_=std::move(out);
 }
private: std::vector<DamageRegion> dirty_{};
};
} // namespace ttytk
