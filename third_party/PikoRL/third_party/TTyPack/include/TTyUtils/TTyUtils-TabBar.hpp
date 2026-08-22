#pragma once

#include <TTyUtils/TTyUtils-Menu.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ttyutils::ui {

struct Tab {
    std::string title{};
    bool modified{};
};

enum class TabPolicy { Scroll, Compress, PickList };

class TabBar : public Widget {
public:
    void add(Tab tab) { tabs_.push_back(std::move(tab)); }
    void close(const std::size_t index) { if (index < tabs_.size()) tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index)); }
    void select(const std::size_t index) {
        if (index < tabs_.size()) {
            selected_ = index;
            if (callback_) callback_(static_cast<int>(index));
        }
    }
    void on_select(std::function<void(int)> callback) { callback_ = std::move(callback); }
    [[nodiscard]] auto selected() const noexcept -> std::size_t { return selected_; }
    [[nodiscard]] auto tabs() const noexcept -> const std::vector<Tab>& { return tabs_; }
    void set_policy(const TabPolicy policy) noexcept { policy_ = policy; }

private:
    std::vector<Tab> tabs_;
    std::size_t selected_{};
    TabPolicy policy_{TabPolicy::Scroll};
    std::function<void(int)> callback_{};
};

}  // namespace ttyutils::ui
