#pragma once

#include <TTyTk/TTyTk-CellBuffer.hpp>
#include <TTyTk/TTyTk-ColorTools.hpp>
#include <TTyUtils/TTyUtils-Theme.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttyutils::ui {

// NOTE(agent): TTyTk::Screen is supplied by the compatibility façade in
// TTyTk-CellBuffer.hpp. Canvas still records clipped commands for widgets.
struct Rect {
    int x{};
    int y{};
    int width{};
    int height{};

    [[nodiscard]] constexpr auto contains(const int px, const int py) const noexcept -> bool {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

struct SizeHint {
    struct Size {
        int width{};
        int height{};
    };
    Size min{};
    Size preferred{};
    int stretch{1};
};

class Canvas {
public:
    struct Text {
        int x{};
        int y{};
        std::string value{};
        theme::Style style{};
    };

    explicit Canvas(const Rect clip = {}) : clip_(clip) {}
    void text(const int x, const int y, const std::string_view value, const theme::Style& style = {}) {
        if (!clip_.contains(x, y)) return;
        texts_.push_back({x, y, std::string(value), style});
    }
    [[nodiscard]] auto texts() const noexcept -> const std::vector<Text>& { return texts_; }
    [[nodiscard]] auto clip() const noexcept -> Rect { return clip_; }

private:
    Rect clip_;
    std::vector<Text> texts_;
};

class Widget;
struct HitTest {
    Widget* widget{};
};
using DamageList = std::vector<Rect>;

class Widget {
public:
    virtual ~Widget() = default;

    virtual void paint(Canvas&) {}
    [[nodiscard]] virtual auto hint() const -> SizeHint { return {}; }

    void set_rect(const Rect rect) noexcept { rect_ = rect; }
    [[nodiscard]] auto rect() const noexcept -> Rect { return rect_; }
    void set_style(theme::Style style) { style_ = std::move(style); }
    [[nodiscard]] auto style() const noexcept -> const theme::Style& { return style_; }

    void add_child(std::unique_ptr<Widget> child) {
        if (child) {
            child->parent_ = this;
            children_.push_back(std::move(child));
            damage_.push_back(rect_);
        }
    }
    [[nodiscard]] auto children() const noexcept -> const std::vector<std::unique_ptr<Widget>>& { return children_; }
    [[nodiscard]] auto parent() const noexcept -> Widget* { return parent_; }

    void render(Canvas& canvas) {
        paint(canvas);
        for (auto& child : children_) child->render(canvas);
        damage_.clear();
    }

    [[nodiscard]] auto hit_test(const int x, const int y) -> HitTest {
        for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
            if (const auto hit = (*it)->hit_test(x, y); hit.widget != nullptr) return hit;
        }
        return rect_.contains(x, y) ? HitTest{this} : HitTest{};
    }

    void invalidate(const Rect rect) { damage_.push_back(rect); }
    [[nodiscard]] auto damage() const noexcept -> const DamageList& { return damage_; }
    void on_focus(std::function<void(bool)> callback) { focus_callback_ = std::move(callback); }
    void set_focused(const bool focused) {
        focused_ = focused;
        if (focus_callback_) focus_callback_(focused);
    }
    [[nodiscard]] auto focused() const noexcept -> bool { return focused_; }

private:
    Rect rect_{};
    theme::Style style_{};
    Widget* parent_{};
    std::vector<std::unique_ptr<Widget>> children_;
    DamageList damage_;
    std::function<void(bool)> focus_callback_{};
    bool focused_{};
};

}  // namespace ttyutils::ui
