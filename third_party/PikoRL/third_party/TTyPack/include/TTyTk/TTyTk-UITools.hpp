#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-CellBuffer.hpp>
#include <TTyTk/TTyTk-RectOps.hpp>
#include <TTyTk/TTyTk-TextLayout.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttytk::ui {

namespace detail {
[[nodiscard]] inline auto repeat(const std::string_view value, const std::size_t count) -> std::string {
    std::string output;
    for (std::size_t index = 0; index < count; ++index) output += value;
    return output;
}
}  // namespace detail

// NOTE(agent): The manifest example names DoubleBorder without defining its
// type; this enum is the smallest public border choice needed by Box.
enum Border { SingleBorder, DoubleBorder };

class Box {
public:
    explicit Box(const Border border = SingleBorder) : border_(border) {}

    [[nodiscard]] inline auto draw(const std::size_t width, const std::size_t height) const
        -> std::vector<std::string> {
        if (width == 0 || height == 0) return {};
        const bool doubled = border_ == DoubleBorder;
        const std::string horizontal = doubled ? "\xe2\x95\x90" : "\xe2\x94\x80";
        const std::string vertical = doubled ? "\xe2\x95\x91" : "\xe2\x94\x82";
        const std::string top_left = doubled ? "\xe2\x95\x94" : "\xe2\x94\x8c";
        const std::string top_right = doubled ? "\xe2\x95\x97" : "\xe2\x94\x90";
        const std::string bottom_left = doubled ? "\xe2\x95\x9a" : "\xe2\x94\x94";
        const std::string bottom_right = doubled ? "\xe2\x95\x9d" : "\xe2\x94\x98";
        std::vector<std::string> lines;
        lines.push_back(top_left + detail::repeat(horizontal, width > 1 ? width - 2 : 0) + top_right);
        for (std::size_t row = 2; row < height; ++row)
            lines.push_back(vertical + std::string(width > 1 ? width - 2 : 0, ' ') + vertical);
        if (height > 1)
            lines.push_back(bottom_left + detail::repeat(horizontal, width > 1 ? width - 2 : 0) + bottom_right);
        return lines;
    }

private:
    Border border_{};
};

class Label {
public:
    explicit Label(std::string text = {}) : text_(std::move(text)) {}
    [[nodiscard]] inline auto text() const noexcept -> std::string_view { return text_; }
    inline void set_text(std::string text) { text_ = std::move(text); }
private:
    std::string text_{};
};

class Layout {
public:
    inline void add(Label label) { labels_.push_back(std::move(label)); }
    [[nodiscard]] inline auto labels() const noexcept -> const std::vector<Label>& { return labels_; }
private:
    std::vector<Label> labels_{};
};

class Panel {
public:
    Panel() = default;
    explicit Panel(Box box) : box_(std::move(box)) {}
    [[nodiscard]] inline auto box() const noexcept -> const Box& { return box_; }
private:
    Box box_{};
};

struct Theme {
    color::Rgb foreground{255, 255, 255};
    color::Rgb background{};
};

}  // namespace ttytk::ui
