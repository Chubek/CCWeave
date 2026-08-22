#pragma once

#include <TTyUtils/TTyUtils-Widget.hpp>

#include <cstddef>
#include <vector>

namespace ttyutils::ui {

enum Orientation { Vertical, Horizontal };

struct ScrollMark {
    std::size_t offset{};
    theme::Style style{};
};

class Scrollable {
public:
    virtual ~Scrollable() = default;
    [[nodiscard]] virtual auto offset() const -> std::size_t = 0;
    virtual void scroll_to(std::size_t offset) = 0;
    [[nodiscard]] virtual auto length() const -> std::size_t = 0;
};

class ScrollBar : public Widget {
public:
    explicit ScrollBar(const Orientation orientation = Vertical) : orientation_(orientation) {}
    void attach(Scrollable& scrollable) { scrollable_ = &scrollable; }
    void set_marks(std::vector<ScrollMark> marks) { marks_ = std::move(marks); }
    [[nodiscard]] auto fraction() const noexcept -> double {
        return scrollable_ == nullptr || scrollable_->length() == 0
                   ? 0.0 : static_cast<double>(scrollable_->offset()) / scrollable_->length();
    }
    void scroll_by(const int amount) {
        if (scrollable_ == nullptr) return;
        const auto current = static_cast<long long>(scrollable_->offset());
        const auto next = std::max<long long>(0, current + amount);
        scrollable_->scroll_to(static_cast<std::size_t>(next));
    }

private:
    Orientation orientation_;
    Scrollable* scrollable_{};
    std::vector<ScrollMark> marks_;
};

}  // namespace ttyutils::ui
