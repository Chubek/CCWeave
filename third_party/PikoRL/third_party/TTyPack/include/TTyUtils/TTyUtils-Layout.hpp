#pragma once

#include <TTyUtils/TTyUtils-Widget.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace ttyutils::ui {

struct Constraint {
    int minimum{};
    int maximum{0x3fffffff};
    int stretch{1};
};

class HBox : public Widget {
public:
    HBox() = default;
    explicit HBox(std::vector<std::unique_ptr<Widget>> children) {
        for (auto& child : children) add_child(std::move(child));
    }

    void layout() {
        const auto bounds = rect();
        const auto& children = this->children();
        if (children.empty()) return;
        const auto width = std::max(0, bounds.width);
        const auto each = width / static_cast<int>(children.size());
        auto remainder = width % static_cast<int>(children.size());
        int x = bounds.x;
        for (const auto& child : children) {
            const auto assigned = each + (remainder-- > 0 ? 1 : 0);
            child->set_rect({x, bounds.y, assigned, bounds.height});
            if (const auto* box = dynamic_cast<const HBox*>(child.get()); box != nullptr)
                (void)box;
            x += assigned;
        }
    }
};

class VBox : public Widget {
public:
    VBox() = default;
    explicit VBox(std::vector<std::unique_ptr<Widget>> children) {
        for (auto& child : children) add_child(std::move(child));
    }

    void layout() {
        const auto bounds = rect();
        const auto& children = this->children();
        if (children.empty()) return;
        const auto height = std::max(0, bounds.height);
        const auto each = height / static_cast<int>(children.size());
        auto remainder = height % static_cast<int>(children.size());
        int y = bounds.y;
        for (const auto& child : children) {
            const auto assigned = each + (remainder-- > 0 ? 1 : 0);
            child->set_rect({bounds.x, y, bounds.width, assigned});
            y += assigned;
        }
    }
};

class Grid : public Widget {
public:
    Grid(const std::size_t rows = 1, const std::size_t columns = 1)
        : rows_(std::max<std::size_t>(rows, 1)), columns_(std::max<std::size_t>(columns, 1)) {}

    void layout() {
        const auto bounds = rect();
        const auto cell_width = bounds.width / static_cast<int>(columns_);
        const auto cell_height = bounds.height / static_cast<int>(rows_);
        const auto& children = this->children();
        for (std::size_t index = 0; index < children.size(); ++index) {
            const auto row = index / columns_;
            const auto column = index % columns_;
            if (row >= rows_) break;
            children[index]->set_rect({bounds.x + static_cast<int>(column) * cell_width,
                                       bounds.y + static_cast<int>(row) * cell_height,
                                       cell_width, cell_height});
        }
    }

private:
    std::size_t rows_;
    std::size_t columns_;
};

class Stack : public Widget {
public:
    void layout() {
        for (const auto& child : children()) child->set_rect(rect());
    }
};

class Spacer : public Widget {
public:
    explicit Spacer(const int stretch = 1) : stretch_(stretch) {}
    [[nodiscard]] auto hint() const -> SizeHint override { return {{0, 0}, {0, 0}, stretch_}; }

private:
    int stretch_;
};

class Split : public Widget {
public:
    enum class Direction { horizontal, vertical };

    explicit Split(const Direction direction = Direction::horizontal, const int ratio = 50)
        : direction_(direction), ratio_(std::clamp(ratio, 0, 100)) {}

    void layout() {
        const auto bounds = rect();
        const auto& children = children_;
        if (children.size() < 2) return;
        if (direction_ == Direction::horizontal) {
            const auto first = bounds.width * ratio_ / 100;
            children[0]->set_rect({bounds.x, bounds.y, first, bounds.height});
            children[1]->set_rect({bounds.x + first, bounds.y, bounds.width - first, bounds.height});
        } else {
            const auto first = bounds.height * ratio_ / 100;
            children[0]->set_rect({bounds.x, bounds.y, bounds.width, first});
            children[1]->set_rect({bounds.x, bounds.y + first, bounds.width, bounds.height - first});
        }
    }

    void set_ratio(const int ratio) noexcept { ratio_ = std::clamp(ratio, 0, 100); }
    [[nodiscard]] auto ratio() const noexcept -> int { return ratio_; }

private:
    // NOTE(agent): Widget exposes children only as a const container, but its
    // child pointers remain mutable; this alias avoids a new public tree API.
    const std::vector<std::unique_ptr<Widget>>& children_ = children();
    Direction direction_;
    int ratio_;
};

}  // namespace ttyutils::ui
